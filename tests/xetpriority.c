/*
 * Copyright (c) 2016-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "scno.h"
#include "pidns.h"

#if defined __NR_getpriority && defined __NR_setpriority

# include <stdio.h>
# include <sys/resource.h>
# include <unistd.h>

int
main(void)
{
#ifdef PIDNS_TRANSLATION
	pidns_test_init();
#endif

	const int pid = getpid();
	const int pgid = getpgid(0);

	long rc = syscall(__NR_getpriority, PRIO_PROCESS,
			  F8ILL_KULONG_MASK | pid);
	pidns_printf("getpriority(PRIO_PROCESS, %d%s) = %ld\n",
		pid, pidns_pid2str(PT_TGID), rc);

	rc = syscall(__NR_setpriority, PRIO_PROCESS,
		     F8ILL_KULONG_MASK | pid, F8ILL_KULONG_MASK);
	pidns_printf("setpriority(PRIO_PROCESS, %d%s, 0) = %s\n",
		pid, pidns_pid2str(PT_TGID), sprintrc(rc));

	rc = syscall(__NR_getpriority, PRIO_PGRP,
			  F8ILL_KULONG_MASK | pgid);
	pidns_printf("getpriority(PRIO_PGRP, %d%s) = %ld\n",
		pgid, pidns_pid2str(PT_PGID), rc);

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_getpriority && _NR_setpriority")

#endif
