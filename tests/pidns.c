/*
 * Test PID namespace translation
 *
 * Copyright (c) 2020 Ákos Uzonyi <uzonyi.akos@gmail.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#include "tests.h"
#include "scno.h"
#include "limits.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <sched.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#define CHLID_STACK_SIZE (1 << 15)

enum pid_type {
	PT_TID,
	PT_TGID,
	PT_PGID,
	PT_SID,

	PT_COUNT,
	PT_NONE = -1
};

struct id_pair {
	pid_t id;
	pid_t strace_id;
};

static void
child_print_leader(struct id_pair *ids)
{
	printf("%-5d ", ids[PT_TID].strace_id);
}

static void
print_id_pair(struct id_pair id_pair)
{
	printf("%d", id_pair.id);
	if (id_pair.id != id_pair.strace_id)
		printf(" /* %d in strace's PID NS */", id_pair.strace_id);
}

static pid_t
do_clone(int (*func)(void *), void *arg)
{
	char *stack_end = (char *) malloc(CHLID_STACK_SIZE) + CHLID_STACK_SIZE;
	fflush(stdout);
	pid_t pid = clone(func, stack_end, SIGCHLD, arg);
	if (pid < 0)
		perror_msg_and_fail("clone");

	return pid;
}

static int
child_fn(void* arg)
{
	struct id_pair ids[PT_COUNT];

	int *child_pipe = (int *) arg;
	read(child_pipe[0], ids, sizeof(ids));
	close(child_pipe[0]);
	close(child_pipe[1]);

	if (ids[PT_SID].strace_id) {
		ids[PT_SID].id = setsid();
		ids[PT_PGID].id = ids[PT_SID].id;
		child_print_leader(ids);
		//TODO
		printf("setsid() = %d\n", ids[PT_SID].id);
	}

	ids[PT_TID].id = syscall(__NR_gettid);
	child_print_leader(ids);
	printf("gettid() = ");
	print_id_pair(ids[PT_TID]);
	printf("\n");

	ids[PT_TGID].id = getpid();
	child_print_leader(ids);
	printf("getpid() = ");
	print_id_pair(ids[PT_TGID]);
	printf("\n");

	ids[PT_PGID].id = getpgid(0);
	child_print_leader(ids);
	printf("getpgid(0) = ");
	print_id_pair(ids[PT_PGID]);
	printf("\n");

	getpgrp();
	child_print_leader(ids);
	printf("getpgrp() = ");
	print_id_pair(ids[PT_PGID]);
	printf("\n");

	ids[PT_SID].id = getsid(0);
	child_print_leader(ids);
	printf("getsid(0) = ");
	print_id_pair(ids[PT_SID]);
	printf("\n");

	int rc;
	rc = syscall(__NR_pidfd_open, ids[PT_TGID].id, 0);
	child_print_leader(ids);
	printf("pidfd_open(");
	print_id_pair(ids[PT_TGID]);
	printf(", 0) = %d", rc);
	printf("\n");

	rc = syscall(__NR_pidfd_open, ids[PT_TGID].id, 0);
	child_print_leader(ids);
	printf("pidfd_open(");
	print_id_pair(ids[PT_TGID]);
	printf(", 0) = %d", rc);
	printf("\n");

	struct id_pair kill_id_pair = ids[PT_PGID];
	kill_id_pair.id *= -1;
	kill(kill_id_pair.id, 0);
	child_print_leader(ids);
	printf("kill(");
	print_id_pair(kill_id_pair);
	printf(", 0) = 0\n");

	kill(ids[PT_TGID].id, 0);
	child_print_leader(ids);
	printf("kill(");
	print_id_pair(ids[PT_TGID]);
	printf(", 0) = 0\n");

	kill(-1, 0);
	child_print_leader(ids);
	if (ids[PT_PGID].id)
		printf("kill(-1, 0) = 0\n");
	else
		printf("kill(-1, 0) = -1 ESRCH (%s)\n", strerror(ESRCH));

	kill(0, 0);
	child_print_leader(ids);
	printf("kill(0, 0) = 0\n");

	child_print_leader(ids);
	printf("+++ exited with 0 +++\n");
	fflush(stdout);
	return 0;
}

static void
parent_print_leader(void)
{
	static pid_t pid;
	if (!pid) {
		pid = getpid();
		parent_print_leader();
		printf("getpid() = %d\n", pid);
	}
	printf("%-5d ", pid);
}

static pid_t
launch_child(pid_t pgid, bool new_sid)
{
	int child_pipe[2];
	if (pipe(child_pipe) < 0)
		perror_msg_and_fail("pipe");

	pid_t pid = do_clone(child_fn, child_pipe);

	struct id_pair child_ids[PT_COUNT];
	child_ids[PT_TID].strace_id = pid;
	child_ids[PT_TGID].strace_id = pid;

	if (pgid > 0) {
		setpgid(pid, pgid);
		child_ids[PT_PGID].strace_id = pgid;
	} else if (pgid == 0) {
		setpgid(pid, pid);
		child_ids[PT_PGID].strace_id = pid;
	} else {
		child_ids[PT_PGID].strace_id = 0;
	}

	if (new_sid) {
		child_ids[PT_SID].strace_id = pid;
		child_ids[PT_PGID].strace_id = pid;
	} else {
		child_ids[PT_SID].strace_id = 0;
	}

	write(child_pipe[1], child_ids, sizeof(child_ids));
	close(child_pipe[0]);
	close(child_pipe[1]);

	/* WNOWAIT: leave the zombie, to be able to use it as a process group */
	siginfo_t siginfo;
	if (waitid(P_PID, pid, &siginfo, WEXITED | WNOWAIT) < 0)
		perror_msg_and_fail("wait");
	if (siginfo.si_code != CLD_EXITED || siginfo.si_status)
		error_msg_and_fail("child exited with nonzero exit status");


	return pid;
}

static int pause_fn(void *arg)
{
	pause();
	return 0;
}

int
main(void)
{
	unshare(CLONE_NEWPID);

	/* Create sleeping process to keep PID namespace alive */
	pid_t pause_pid = do_clone(pause_fn, NULL);

	launch_child(-1, false);
	launch_child(-1, true);
	pid_t pgid = launch_child(0, false);
	launch_child(pgid, false);
	launch_child(pgid, true);

	parent_print_leader();
	kill(pause_pid, SIGKILL);
	printf("kill(%d, SIGKILL) = 0\n", pause_pid);
	while (wait(NULL) > 0);
	if (errno != ECHILD)
		perror_msg_and_fail("wait");

	parent_print_leader();
	printf("+++ exited with 0 +++\n");
	return 0;
}
