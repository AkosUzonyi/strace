#include "defs.h"


#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <asm/unistd.h>

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "btree.h"
#include "nsfs.h"
#include "xmalloc.h"

struct btree *ns_pid_to_proc_pid[PT_COUNT];
struct btree *proc_data_cache;

static const char tid_str[]  = "NSpid:\t";
static const char tgid_str[] = "NStgid:\t";
static const char pgid_str[] = "NSpgid:\t";
static const char sid_str[]  = "NSsid:\t";

static const struct {
	const char *str;
	size_t size;
} id_strs[PT_COUNT] = {
	[PT_TID] =  { tid_str,  sizeof(tid_str)  - 1 },
	[PT_TGID] = { tgid_str, sizeof(tgid_str) - 1 },
	[PT_PGID] = { pgid_str, sizeof(pgid_str) - 1 },
	[PT_SID] =  { sid_str,  sizeof(sid_str)  - 1 },
};


/**
 * Limit on PID NS hierarchy depth, imposed since Linux 3.7. NS traversal
 * is not possible before Linux 4.9, so we consider this limit pretty universal.
 */
#define MAX_NS_DEPTH 32

struct proc_data {
	int proc_pid;
	int ns_count;
	uint64_t ns_hierarchy[MAX_NS_DEPTH]; /* from bottom to top of NS hierarchy */
	int id_count[PT_COUNT];
	int *id_hierarchy[PT_COUNT]; /* from top to bottom of NS hierarchy */
};

static uint8_t
lg2(uint64_t n)
{
	uint8_t res = 0;
	while (n) {
		res++;
		n >>= 1;
	}
	return res;
}

static int
get_pid_max(void)
{
	static int pid_max = -1;

	if (pid_max < 0) {
		pid_max = INT_MAX;

		FILE *f = fopen("/proc/sys/kernel/pid_max", "r");
		if (!f)
			perror_msg("get_pid_max: opening /proc/sys/kernel/pid_max");
		else
			fscanf(f, "%d", &pid_max);
	}

	return pid_max;
}

void
pidns_init(void)
{
	static bool inited = false;
	if (inited)
		return;

	for (int i = 0; i < PT_COUNT; i++)
		ns_pid_to_proc_pid[i] = btree_create(6, 16, 16, 64, 0);

	proc_data_cache = btree_create(6, 16, 16, lg2(get_pid_max() - 1), 0);

	inited = true;
}

static void
put_proc_pid(uint64_t ns, int ns_pid, enum pid_type type, int proc_pid)
{
	struct btree *b = (struct btree *) btree_get(ns_pid_to_proc_pid[type], ns);
	if (!b) {
		int pid_max = get_pid_max();
		uint8_t pid_max_size = lg2(pid_max - 1);
		uint8_t pid_max_size_lg = lg2(pid_max_size - 1);
		b = btree_create(pid_max_size_lg, 16, 16, pid_max_size, 0);

		btree_set(ns_pid_to_proc_pid[type], ns, (uint64_t) b);
	}
	btree_set(b, ns_pid, proc_pid);
}

static int
get_cached_proc_pid(uint64_t ns, int ns_pid, enum pid_type type)
{
	struct btree *b = (struct btree *) btree_get(ns_pid_to_proc_pid[type], ns);
	if (!b)
		return 0;

	return btree_get(b, ns_pid);
}

/**
 * Helper function, converts pid to string, or to "self" for pid == 0.
 * Uses static buffer for operation.
 */
static const char *
pid_to_str(pid_t pid)
{
	static char buf[sizeof("-2147483648")];
	ssize_t ret;

	if (!pid)
		return "self";

	ret = snprintf(buf, sizeof(buf), "%d", pid);

	if ((ret < 0) || ((size_t) ret >= sizeof(buf)))
		perror_msg_and_die("pid_to_str: snprintf");

	return buf;
}

/**
 * Returns a list of PID NS IDs for the specified PID.
 *
 * @param proc_pid PID (as present in /proc) to get information for.
 * @param ns_buf   Pointer to buffer that is able to contain at least
 *                 MAX_NS_DEPTH items.
 * @param last     ID of NS on which ascencion can be interrupted.
 *                 0 for no interruption.
 * @return         Amount of NS in list. 0 indicates error, MAX_NS_DEPTH + 1
 *                 indicates that ascension limit hasn't been reached (only
 *                 MAX_NS_DEPTH values have been written to the array, however).
 */
static size_t
get_ns_hierarchy(int proc_pid, uint64_t *ns_buf, size_t ns_buf_size)
{
	char path[PATH_MAX + 1];
	struct stat st;
	ssize_t ret;
	size_t n = 0;
	int fd;
	int parent_fd;

	ret = snprintf(path, sizeof(path), "/proc/%s/ns/pid",
		       pid_to_str(proc_pid));

	if ((ret < 0) || ((size_t) ret >= sizeof(path)))
		return 0;

	fd = open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		perror_msg("get_ns_hierarchy: opening /proc/<pid>/ns/pid");
		return 0;
	}

	while (1) {
		ret = fstat(fd, &st);
		if (ret)
			break;

		/* 32 is the hierarchy depth on modern Linux */
		if ((n >= MAX_NS_DEPTH) || (n >= ns_buf_size)) {
			n++;
			break;
		}

		ns_buf[n] = st.st_ino;
		if (debug_flag)
			error_msg("Got NS: %" PRIu64, ns_buf[n]);

		n++;

		parent_fd = ioctl(fd, NS_GET_PARENT);
		if (parent_fd == -1) {
			switch (errno) {
			case EPERM:
				if (debug_flag)
					error_msg("Terminating NS ascending "
						  "after %zu levels on NS %"
						  PRIu64, n, ns_buf[n - 1]);
				break;

			case ENOTTY:
				error_msg("NS_* ioctl commands are not "
					  "supported by the kernel");
				break;
			default:
				perror_msg("get_ns_hierarchy: "
					   "ioctl(NS_GET_PARENT)");
				break;
			}

			break;
		}

		close(fd);
		fd = parent_fd;
	}

	close(fd);

	return n;
}

/**
 * Get list of IDs present in NS* proc status record. IDs are placed as they are
 * stored in /proc (from top to bottom of NS hierarchy).
 *
 * @param proc_pid    PID (as present in /proc) to get information for.
 * @param id_buf      Pointer to buffer that is able to contain at least
 *                    MAX_NS_DEPTH items. Can be NULL.
 * @param type        Type of ID requested.
 * @return            Number of items stored in id_list. 0 indicates error,
 *                    MAX_NS_DEPTH + 1 indicates that status record contains
 *                    more that MAX_NS_DEPTH records and the id_buf provided
 *                    is unusable.
 */
static size_t
get_id_list(int proc_pid, int *id_buf, enum pid_type type)
{
	const char *ns_str = id_strs[type].str;
	size_t ns_str_size = id_strs[type].size;
	char *buf;
	size_t bufsize = 0;
	char *p;
	char *endp;
	FILE *f;
	int idx = 0;
	ssize_t ret;

	ret = asprintf(&buf, "/proc/%s/status", pid_to_str(proc_pid));
	if (ret < 0)
		return 0;

	f = fopen(buf, "r");
	if (!f) {
		perror_msg("get_id_list: opening /proc/<pid>/status");
		return 0;
	}

	free(buf);
	buf = NULL;

	while (getline(&buf, &bufsize, f) > 0) {
		if (strncmp(buf, ns_str, ns_str_size))
			continue;

		p = buf + ns_str_size;

		for (idx = 0; idx < MAX_NS_DEPTH; idx++) {
			if (!p)
				break;

			errno = 0;
			int id = strtol(p, &endp, 10);

			if (errno && (p[0] != '\t')) {
				perror_msg("get_id_list: converting pid to int");
				idx = 0;
				goto get_id_list_exit;
			}

			if (debug_flag)
				error_msg("PID %d: %s[%d]: %d",
					  proc_pid, ns_str, idx, id);

			if (id_buf)
				id_buf[idx] = id;

			strsep(&p, "\t");
		}

		if (p)
			idx++;

		break;
	}

get_id_list_exit:
	if (f)
		fclose(f);
	if (buf)
		free(buf);

	return idx;
}

static bool
is_proc_ours(void)
{
	static int cached_val = -1;

	if (cached_val < 0)
		cached_val = get_id_list(0, NULL, PT_TID) == 1;

	return cached_val;
}

static uint64_t
get_ns(struct tcb *tcp)
{
	if (!tcp->pid_ns_inited) {
		int pid = tcp->pid;

		if (!is_proc_ours())
			if (translate_pid(NULL, tcp->pid, PT_TID, &pid) < 1)
				pid = -1;

		if ((pid == -1) || !get_ns_hierarchy(pid, &tcp->pid_ns, 1))
			tcp->pid_ns = 0;

		tcp->pid_ns_inited = true;
	}

	return tcp->pid_ns;
}

static uint64_t
get_our_ns(void)
{
	static uint64_t our_ns = 0;
	static bool our_ns_initialised = false;

	if (!our_ns_initialised) {
		get_ns_hierarchy(0, &our_ns, 1);
		our_ns_initialised = true;
	}

	return our_ns;
}

/**
 * Returns the cached proc_data struct associated with proc_pid.
 * If none found, allocates a new proc_data.
 */
static struct proc_data *
get_or_create_proc_data(int proc_pid)
{
	struct proc_data *pd = (struct proc_data *) btree_get(proc_data_cache, proc_pid);

	if (!pd) {
		pd = calloc(1, sizeof(*pd));
		if (!pd)
			return NULL;

		pd->proc_pid = proc_pid;
		btree_set(proc_data_cache, proc_pid, (uint64_t) pd);
	}

	return pd;
}

/**
 * Updates the proc_data from /proc
 * If the process does not exists, returns false, and frees the proc_data
 */
static bool
update_proc_data(struct proc_data *pd, enum pid_type type)
{
	pd->ns_count = get_ns_hierarchy(pd->proc_pid,
		pd->ns_hierarchy, MAX_NS_DEPTH);
	if (!pd->ns_count)
		goto fail;

	if (!pd->id_hierarchy[type])
		pd->id_hierarchy[type] = calloc(MAX_NS_DEPTH,
			sizeof(pd->id_hierarchy[type][0]));
	if (!pd->id_hierarchy[type])
		goto fail;

	pd->id_count[type] = get_id_list(pd->proc_pid,
		pd->id_hierarchy[type], type);
	if (!pd->id_count[type])
		goto fail;

	return true;

fail:
	if (pd)
		free(pd);

	btree_set(proc_data_cache, pd->proc_pid, (uint64_t) NULL);
	return false;
}

/**
 * Paramters for id translation
 */
struct translate_id_params {
	/* The result (output) */
	int result_id;
	/* The proc data of the process (output) */
	struct proc_data *pd;

	/* The namespace to be translated from */
	uint64_t from_ns;
	/* The id to be translated */
	int from_id;
	/* The type of the id */
	enum pid_type type;
};

/**
 * Translates an id to our namespace, given the proc_pid of the process, by reading files in /proc.
 *
 * @param tip      The parameters
 * @param proc_pid The proc pid of the process. If 0, use the cached values in tip->pd.
 */
static void
translate_id_proc_pid(struct translate_id_params *tip, int proc_pid)
{
	struct proc_data *pd = proc_pid ? get_or_create_proc_data(proc_pid) : tip->pd;

	tip->result_id = 0;
	tip->pd = NULL;

	if (!pd)
		return;

	if (proc_pid && !update_proc_data(pd, tip->type))
		return;

	if (!pd->ns_count || (pd->id_count[tip->type] < pd->ns_count))
		return;

	int our_ns_id_idx = pd->id_count[tip->type] - pd->ns_count;

	for (int i = 0; i < pd->ns_count; i++) {
		if (pd->ns_hierarchy[i] != tip->from_ns)
			continue;

		int id_idx = pd->id_count[tip->type] - i - 1;
		if (pd->id_hierarchy[tip->type][id_idx] != tip->from_id)
			return;

		tip->result_id = pd->id_hierarchy[tip->type][our_ns_id_idx];
		tip->pd = pd;
		return;
	}
}

/**
 * Translates an id to our namespace, by reading all proc entries in dir.
 *
 * @param tip            The parameters
 * @param path           The path of the dir to be read.
 * @param read_task_dir  Whether recurse to "task" subdirectory.
 */
static void
translate_id_dir(struct translate_id_params *tip, const char *path, bool read_task_dir)
{
	DIR *dir = opendir(path);
	if (!dir) {
		perror_msg("translate_id_dir: opening dir: %s", path);
		return;
	}

	while (!tip->result_id) {
		errno = 0;
		struct dirent *entry = readdir(dir);
		if (!entry) {
			if (errno)
				perror_msg("translate_id_dir: readdir");

			break;
		}

		if (entry->d_type != DT_DIR)
			continue;

		errno = 0;
		int proc_pid = strtol(entry->d_name, NULL, 10);
		if (errno)
			continue;
		if ((proc_pid < 1) || (proc_pid > INT_MAX))
			continue;

		if (read_task_dir) {
			char task_dir_path[PATH_MAX + 1];
			snprintf(task_dir_path, sizeof(task_dir_path), "/proc/%d/task", proc_pid);
			translate_id_dir(tip, task_dir_path, false);
		}

		if (tip->result_id)
			break;

		translate_id_proc_pid(tip, proc_pid);
	}

	closedir(dir);
}

/**
 * Iterator function of the proc_data_cache for id translation.
 * If the cache contains the id we are looking for, reads the corresponding
 * directory in /proc, and if cache is valid, saves the result.
 */
static void
proc_data_cache_iterator_fn(void* fn_data, uint64_t key, uint64_t val)
{
	struct translate_id_params *tip = (struct translate_id_params *) fn_data;
	struct proc_data *pd = (struct proc_data *) val;

	if (!pd)
		return;

	/* Result already found in an earlier iteration */
	if (tip->result_id)
		return;

	/* Translate from cache */
	tip->pd = pd;
	translate_id_proc_pid(tip, 0);
	if (!tip->result_id)
		return; /* According to cache, this is not what we are looking for, continue */

	/* Now translate it from actual data in /proc, to check cache validity */
	translate_id_proc_pid(tip, pd->proc_pid);
}

/**
 * Translates an ID from tcp's namespace to our namepace
 *
 * @param tcp             The tcb whose namepace from_id is in (NULL means strace's namespace)
 * @param from_id         The id to be translated
 * @param type            The type of ID
 * @param proc_pid_ptr    If not NULL, writes the proc PID to this location
 */
int
translate_pid(struct tcb *tcp, int from_id, enum pid_type type, int *proc_pid_ptr)
{
	const uint64_t our_ns = get_our_ns();

	struct translate_id_params tip = {
		.result_id = 0,
		.pd = NULL,
		.from_ns = tcp ? get_ns(tcp) : our_ns,
		.from_id = from_id,
		.type = type,
	};

	if ((tip.type >= PT_COUNT) || (tip.type < 0))
		goto translate_pid_exit;

	/* If translation is trivial */
	if (tip.from_ns == our_ns && (is_proc_ours() || !proc_pid_ptr)) {
		if (proc_pid_ptr)
			*proc_pid_ptr = from_id;

		tip.result_id = tip.from_id;
		goto translate_pid_exit;
	}

	/* Look for a cached proc_pid for this (from_ns, from_id) pair */
	int cached_proc_pid = get_cached_proc_pid(tip.from_ns, tip.from_id, tip.type);
	if (cached_proc_pid) {
		translate_id_proc_pid(&tip, cached_proc_pid);
		if (tip.result_id)
			goto translate_pid_exit;
	}

	/* Iterate through the cache, find potential proc_data */
	btree_iterate_keys(proc_data_cache, 0, get_pid_max(), 0, proc_data_cache_iterator_fn, &tip);
	/* (proc_data_cache_iterator_fn takes care about updating proc_data) */
	if (tip.result_id)
		goto translate_pid_exit;

	/* No cache helped, read all entries in /proc */
	translate_id_dir(&tip, "/proc", true);

translate_pid_exit:
	if (tip.pd) {
		if (tip.pd->proc_pid)
			put_proc_pid(tip.from_ns, tip.from_id, tip.type, tip.pd->proc_pid);

		if (proc_pid_ptr)
			*proc_pid_ptr = tip.pd->proc_pid;
	}

	return tip.result_id;
}

int
get_proc_pid(struct tcb *tcp)
{
	if (!is_proc_ours()) {
		int ret;

		if (translate_pid(NULL, tcp->pid, PT_TID, &ret) < 0)
			return -1;

		return ret;
	}

	return tcp->pid;
}

void
printpid(struct tcb *tcp, int pid, enum pid_type type)
{
	int strace_pid;

	tprintf("%d", pid);

	if (perform_ns_resolution) {
		strace_pid = translate_pid(tcp, pid, type, NULL);

		if ((strace_pid > 0) && (pid != strace_pid))
			tprintf_comment("%d in strace's PID NS", strace_pid);
	}
}
