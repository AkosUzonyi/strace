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

bool pidns_translation = false;
bool pidns_unshared = false;

/* Our PIDs in strace's namespace */
pid_t pidns_strace_ids[PT_COUNT];

void
pidns_printf(const char *format, ...)
{
	if (pidns_translation)
		printf("%-5d ", pidns_strace_ids[PT_TID]);

	va_list args;
	va_start(args, format);
	vprintf(format, args);
	va_end(args);
}

const char *
pidns_pid2str(enum pid_type type)
{
	static const char format[] = " /* %d in strace's PID NS */";
	static char buf[sizeof(format) + sizeof(int) * 3];

	if (type < 0 || type >= PT_COUNT)
		return "";

	if (!pidns_unshared || !pidns_strace_ids[type])
		return "";

	snprintf(buf, sizeof(buf), format, pidns_strace_ids[type]);
	return buf;
}

static pid_t
pidns_fork(int *strace_ids_pipe, pid_t pgid, bool new_sid)
{
	if (pipe(strace_ids_pipe) < 0)
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

	write(strace_ids_pipe[1], pidns_strace_ids, sizeof(pidns_strace_ids));
	close(strace_ids_pipe[0]);
	close(strace_ids_pipe[1]);

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
	pidns_translation = true;

	int strace_ids_pipe[2];

	if (!pidns_fork(strace_ids_pipe, -1, false))
		goto pidns_test_init_run_test;

	/* Unshare user namespace too, so we do not need to be root */
	if (unshare(CLONE_NEWUSER | CLONE_NEWPID) < 0)
		perror_msg_and_fail("unshare");

	pidns_unshared = true;

	/* Create sleeping process to keep PID namespace alive */
	pid_t pause_pid = fork();
	if (!pause_pid) {
		pause();
		_exit(0);
	}

	if (!pidns_fork(strace_ids_pipe, -1, false))
		goto pidns_test_init_run_test;

	if (!pidns_fork(strace_ids_pipe, -1, true))
		goto pidns_test_init_run_test;

	pid_t pgid;
	if (!(pgid = pidns_fork(strace_ids_pipe, 0, false)))
		goto pidns_test_init_run_test;

	if (!pidns_fork(strace_ids_pipe, pgid, false))
		goto pidns_test_init_run_test;

	kill(pause_pid, SIGKILL);
	while (wait(NULL) > 0);
	if (errno != ECHILD)
		perror_msg_and_fail("wait");

	exit(0);

pidns_test_init_run_test:
	read(strace_ids_pipe[0], pidns_strace_ids, sizeof(pidns_strace_ids));
	close(strace_ids_pipe[0]);
	close(strace_ids_pipe[1]);

	if (pidns_strace_ids[PT_SID])
		setsid();
}
