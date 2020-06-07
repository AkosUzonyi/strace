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

static int
proc_data_get_id_in_our_ns(struct proc_data *pd, uint64_t ns, int id, enum pid_type type)
{
	if (!pd->ns_count || (pd->ns_count > pd->id_count[type]))
		return 0;

	for (int i = 0; i < pd->ns_count; i++) {
		if (pd->ns_hierarchy[i] != ns)
			continue;

		if (pd->id_hierarchy[type][pd->id_count[type] - i - 1] != id)
			return 0;

		return pd->id_hierarchy[type][pd->id_count[type] - pd->ns_count];
	}

	return 0;
}

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

	//update_ns_hierarchy

	//parent_fd = ge

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
			if (find_pid(NULL, tcp->pid, PT_TID, &pid) < 1)
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
 * Returns ID in our NS. If orig_ns_id is provided, also returns ID in orig_ns.
 */
/* static int
dens_id(int proc_pid,
	uint64_t *ns_buf, size_t ns_count,
	int *id_buf, size_t id_count,
	uint64_t orig_ns, uint64_t our_ns, int *orig_ns_id)
{
	bool orig_idx_found = false;
	size_t idx;

	if (!ns_count || (ns_count > MAX_NS_DEPTH) ||
	    !id_count || (id_count > MAX_NS_DEPTH))
		return -1;

	if (is_proc_ours()) {
	}

	for (idx = 0; idx < ns_count; idx++) {
		if (ns_buf[idx] != orig_ns)
			continue;

		orig_idx = idx;
		orig_idx_found = true;
		break;
	}

	if (!orig_idx_found) {
		free(ns_buf);

		return -1;
	}

} */

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
 * Removes references to the proc_data entry from all caches.
 */
/*
static void
invalidate_proc_data(struct proc_data *pd)
{
}
*/

/**
 * Caches:
 *  * tidns:ns -> tid in our ns
 *   * How to check validity: get cached proc path, with additional check for
 *     ns and that it also has tidns at the relevant level in NSpid
 *  * tid (in our ns) -> proc_tid
 *   * How to check validity: open cached /proc/pid/status and check relevant
 *     NSpid record, check that /proc/pid/ns/pid is accessible [and leads to our
 *     ns]
 *
 *  Tracees have fixed pid ns.
 */

struct find_pid_data {
	int result_id;
	struct proc_data *pd;
	uint64_t dest_ns;
	int dest_id;
	enum pid_type type;
};

static void
proc_data_cache_iterator_fn(void* fn_data, uint64_t key, uint64_t val)
{
	struct find_pid_data *fpd = (struct find_pid_data *) fn_data;
	struct proc_data *pd = (struct proc_data *) val;

	if (!pd)
		return;

	/* Result already found in an earlier iteration */
	if (fpd->result_id)
		return;

	fpd->result_id = proc_data_get_id_in_our_ns(pd, fpd->dest_ns, fpd->dest_id, fpd->type);
	if (!fpd->result_id)
		return; /* According to cache, this is not what we are looking for, continue */

	/* If cache says this is our proc_data, update it to be sure */

	if (!update_proc_data(pd, fpd->type))
		return; /* Process exited, continue */

	fpd->result_id = proc_data_get_id_in_our_ns(pd, fpd->dest_ns, fpd->dest_id, fpd->type);
	if (!fpd->result_id)
		return; /* Cache was wrong, proc_data changed, continue */

	/* Cache was right, save our pd as result */
	fpd->pd = pd;
}

/**
 * Translates an ID from tcp's namespace to our namepace
 *
 * @param tcp             The tcb whose namepace dest_id is in (NULL means strace's namespace)
 * @param dest_id         The id to be translated
 * @param type            The type of ID
 * @param proc_pid_ptr    If not NULL, writes the proc PID to this location
 */
int
find_pid(struct tcb *tcp, int dest_id, enum pid_type type, int *proc_pid_ptr)
{
	const uint64_t our_ns = get_our_ns();
	uint64_t dest_ns;

	struct proc_data *pd;

	DIR *dp = NULL;
	struct dirent *entry;
	long proc_pid = 0;
	int res = 0;

	if ((type >= PT_COUNT) || (type < 0))
		goto find_pid_exit;

	dest_ns = tcp ? get_ns(tcp) : our_ns;

	if (is_proc_ours() && (dest_ns == our_ns)) {
		if (proc_pid_ptr)
			*proc_pid_ptr =
				dest_id ? dest_id : syscall(__NR_gettid);

		if (dest_id)
			return dest_id;

		switch (type) {
		case PT_TID:	return syscall(__NR_gettid);
		case PT_TGID:	return getpid();
		case PT_PGID:	return getpgrp();
		case PT_SID:	return getsid(getpid());
		default:	return -1;
		}
	}

	/* Look for a cached proc_pid for this (dest_ns, dest_id) pair */
	proc_pid = get_cached_proc_pid(dest_ns, dest_id, type);
	if (proc_pid) {
		pd = get_or_create_proc_data(proc_pid);
		if (!pd)
			goto find_pid_exit;

		/* Refresh proc_data, and if it is still valid, return */
		if (update_proc_data(pd, type)) {
			res = proc_data_get_id_in_our_ns(pd, dest_ns, dest_id, type);
			if (res)
				goto find_pid_exit;
		}
	}

	/* Iterate through the cache, find potential proc_data */
	struct find_pid_data fpd = {0, NULL, dest_ns, dest_id, type};
	btree_iterate_keys(proc_data_cache, 0, get_pid_max(), 0, proc_data_cache_iterator_fn, &fpd);
	/* (proc_data_cache_iterator_fn takes care about updating proc_data) */
	if (fpd.result_id) {
		proc_pid = fpd.pd->proc_pid;
		res = fpd.result_id;
		goto find_pid_exit;
	}

	dp = opendir("/proc");
	if (!dp) {
		perror_msg("find_pid: opening /proc");
		goto find_pid_exit;
	}

	while (!res) {
		errno = 0;
		entry = readdir(dp);
		if (!entry) {
			if (errno)
				perror_msg("find_pid: readdir");

			goto find_pid_exit;
		}

		if (entry->d_type != DT_DIR)
			continue;

		errno = 0;
		proc_pid = strtol(entry->d_name, NULL, 10);
		if (errno)
			continue;
		if ((proc_pid < 1) || (proc_pid > INT_MAX))
			continue;

		pd = get_or_create_proc_data(proc_pid);
		if (!pd)
			goto find_pid_exit;

		if (update_proc_data(pd, type))
			res = proc_data_get_id_in_our_ns(pd, dest_ns, dest_id, type);
	}

find_pid_exit:
	if (dp)
		closedir(dp);

	if (proc_pid)
		put_proc_pid(dest_ns, dest_id, type, proc_pid);

	if (proc_pid_ptr)
		*proc_pid_ptr = proc_pid;

	return res;
}

int
get_proc_pid(struct tcb *tcp)
{
	if (!is_proc_ours()) {
		int ret;

		if (find_pid(NULL, tcp->pid, PT_TID, &ret) < 0)
			return -1;

		return ret;
	}

	return tcp->pid;
}

/* To be called on tracee exits or other clear indications that pid is no more
 * relevant */
void
clear_proc_pid(struct tcb *tcp, int pid)
{
}

void
printpid(struct tcb *tcp, int pid, enum pid_type type)
{
	int strace_pid;

	tprintf("%d", pid);

	if (perform_ns_resolution) {
		strace_pid = find_pid(tcp, pid, type, NULL);

		if ((strace_pid > 0) && (pid != strace_pid))
			tprintf_comment("%d in strace's PID NS", strace_pid);
	}
}
