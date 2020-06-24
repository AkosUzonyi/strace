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
#include "pidns.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <stdlib.h>
#include <sched.h>
#include <stdarg.h>

#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

pid_t pidns_ids[PT_COUNT];
pid_t pidns_strace_ids[PT_COUNT];

void
pidns_printf(const char *format, ...)
{
	printf("%-5d ", pidns_strace_ids[PT_TID]);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

const char *
pidns_pid2str(enum pid_type type)
{
	static char buf[sizeof(" /*  in strace's PID NS */") + sizeof(int) * 6];
	int len = snprintf(buf, sizeof(buf), "%d", pidns_ids[type]);
	if (pidns_ids[type] != pidns_strace_ids[type])
		snprintf(buf + len, sizeof(buf) - len, " /* %d in strace's PID NS */", pidns_strace_ids[type]);

	return buf;
}

static void
fill_ids(int *child_pipe)
{
	if (child_pipe) {
		read(child_pipe[0], pidns_strace_ids, sizeof(pidns_strace_ids));
		close(child_pipe[0]);
		close(child_pipe[1]);

		if (pidns_strace_ids[PT_SID]) {
			pidns_ids[PT_SID] = setsid();
			//TODO
			pidns_printf("setsid() = %d\n", pidns_ids[PT_SID]);
		}
	}

	pidns_ids[PT_TID] = syscall(__NR_gettid);
	pidns_ids[PT_TGID] = getpid();
	pidns_ids[PT_PGID] = getpgid(0);
	pidns_ids[PT_SID] = getsid(0);
	getpgrp();

	if (!child_pipe) {
		for (int i = 0; i < PT_COUNT; i++)
			pidns_strace_ids[i] = pidns_ids[i];
	}

	pidns_printf("gettid() = %s\n", pidns_pid2str(PT_TID));
	pidns_printf("getpid() = %s\n", pidns_pid2str(PT_TGID));
	pidns_printf("getpgid(0) = %s\n", pidns_pid2str(PT_PGID));
	pidns_printf("getsid(0) = %s\n", pidns_pid2str(PT_SID));
	pidns_printf("getpgrp() = %s\n", pidns_pid2str(PT_PGID));
}

static pid_t
fork_child(int *child_pipe, pid_t pgid, bool new_sid)
{
	if (child_pipe && pipe(child_pipe) < 0)
		perror_msg_and_fail("pipe");

	fflush(stdout);
	pid_t pid = fork();
	if (pid < 0)
		perror_msg_and_fail("fork");
	if (!pid)
		return 0;

	pidns_strace_ids[PT_TID] = pid;
	pidns_strace_ids[PT_TGID] = pid;
	pidns_strace_ids[PT_PGID] = 0;
	pidns_strace_ids[PT_SID] = 0;

	if (!pgid)
		pgid = pid;

	if (pgid > 0) {
		if (setpgid(pid, pgid) < 0)
			perror_msg_and_fail("setpgid");

		pidns_strace_ids[PT_PGID] = pgid;
	}

	if (new_sid) {
		pidns_strace_ids[PT_SID] = pid;
		pidns_strace_ids[PT_PGID] = pid;
	}

	if (child_pipe) {
		write(child_pipe[1], pidns_strace_ids, sizeof(pidns_strace_ids));
		close(child_pipe[0]);
		close(child_pipe[1]);
	}

	/* WNOWAIT: leave the zombie, to be able to use it as a process group */
	siginfo_t siginfo;
	if (waitid(P_PID, pid, &siginfo, WEXITED | WNOWAIT) < 0)
		perror_msg_and_fail("wait");
	if (siginfo.si_code != CLD_EXITED || siginfo.si_status)
		error_msg_and_fail("child terminated with nonzero exit status");

	return pid;
}

void
pidns_test_init(void)
{
	/* Write our PID to log, to be able to filter out our syscalls */
	getpid();

	if (!fork_child(NULL, -1, false)) {
		fill_ids(NULL);
		return;
	}

	if (unshare(CLONE_NEWPID) < 0) {
		if (errno != EPERM)
			perror_msg_and_fail("unshare");

		exit(0);
	}

	/* Create sleeping process to keep PID namespace alive */
	pid_t pause_pid = fork();
	if (!pause_pid) {
		pause();
		_exit(0);
	}

	int child_pipe[2];

	if (!fork_child(child_pipe, -1, false))
		goto pidns_test_init_run_test;

	if (!fork_child(child_pipe, -1, true))
		goto pidns_test_init_run_test;

	pid_t pgid;
	if (!(pgid = fork_child(child_pipe, 0, false)))
		goto pidns_test_init_run_test;

	if (!fork_child(child_pipe, pgid, false))
		goto pidns_test_init_run_test;

	if (!fork_child(child_pipe, pgid, true))
		goto pidns_test_init_run_test;

	kill(pause_pid, SIGKILL);
	printf("%-5d +++ killed by SIGKILL +++\n", pause_pid);
	while (wait(NULL) > 0);
	if (errno != ECHILD)
		perror_msg_and_fail("wait");

	exit(0);

pidns_test_init_run_test:
	fill_ids(child_pipe);
}
