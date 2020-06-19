/*
 * This file is part of execve strace test.
 *
 * Copyright (c) 2020- The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "scno.h"
#include "limits.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
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
print_strace_tid(struct id_pair *ids)
{
	printf("%d ", ids[PT_TID].strace_id);
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
		print_strace_tid(ids);
		printf("setsid() = %d\n", ids[PT_SID].id);
	}

	ids[PT_TID].id = syscall(__NR_gettid);
	print_strace_tid(ids);
	printf("gettid() = ");
	print_id_pair(ids[PT_TID]);
	printf("\n");

	ids[PT_TGID].id = getpid();
	print_strace_tid(ids);
	printf("getpid() = ");
	print_id_pair(ids[PT_TGID]);
	printf("\n");

	ids[PT_PGID].id = getpgid(0);
	print_strace_tid(ids);
	printf("getpgid(0) = ");
	print_id_pair(ids[PT_PGID]);
	printf("\n");

	ids[PT_SID].id = getsid(0);
	print_strace_tid(ids);
	printf("getsid(0) = ");
	print_id_pair(ids[PT_SID]);
	printf("\n");

	print_strace_tid(ids);
	printf("+++ exited with 0 +++\n");
	fflush(stdout);
	return 0;
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
	if (waitid(P_PID, pid, NULL, WEXITED | WNOWAIT) < 0)
		perror_msg_and_fail("wait");

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
	pid_t pid = getpid();
	printf("%d getpid() = %d\n", pid, pid);

	unshare(CLONE_NEWPID);

	/* Create sleeping process, to keep PID namespace alive */
	pid_t pause_pid = do_clone(pause_fn, NULL);

	launch_child(-1, false);
	launch_child(-1, true);
	pid_t tgid = launch_child(0, false);
	launch_child(tgid, false);
	launch_child(tgid, true);

	kill(pause_pid, SIGKILL);
	printf("%d kill(%d, SIGKILL) = 0\n", pid, pause_pid);
	while (wait(NULL) > 0);
	if (errno != ECHILD)
		perror_msg_and_fail("wait");

	printf("%d +++ exited with 0 +++\n", pid);
	return 0;
}
