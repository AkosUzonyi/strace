/*
 * Copyright (c) 2016-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "scno.h"
#include "pidns.h"

#if defined __NR_sched_getscheduler && defined __NR_sched_setscheduler

# include <sched.h>
# include <stdio.h>
# include <unistd.h>

int
main(void)
{
#ifdef PIDNS_TRANSLATION
	pidns_test_init();
#endif

	TAIL_ALLOC_OBJECT_CONST_PTR(struct sched_param, param);
	const int pid = getpid();

	long rc = syscall(__NR_sched_getscheduler, pid);
	const char *scheduler;
	switch (rc) {
		case SCHED_FIFO:
			scheduler = "SCHED_FIFO";
			break;
		case SCHED_RR:
			scheduler = "SCHED_RR";
			break;
# ifdef SCHED_BATCH
		case SCHED_BATCH:
			scheduler = "SCHED_BATCH";
			break;
# endif
# ifdef SCHED_IDLE
		case SCHED_IDLE:
			scheduler = "SCHED_IDLE";
			break;
# endif
# ifdef SCHED_ISO
		case SCHED_ISO:
			scheduler = "SCHED_ISO";
			break;
# endif
# ifdef SCHED_DEADLINE
		case SCHED_DEADLINE:
			scheduler = "SCHED_DEADLINE";
			break;
# endif
		default:
			scheduler = "SCHED_OTHER";
	}
	pidns_printf("sched_getscheduler(%d%s) = %ld (%s)\n",
	       pid, pidns_pid2str(PT_TGID), rc, scheduler);

	rc = syscall(__NR_sched_getscheduler, -1);
	pidns_printf("sched_getscheduler(-1) = %s\n", sprintrc(rc));

	param->sched_priority = -1;

	rc = syscall(__NR_sched_setscheduler, pid, SCHED_FIFO, NULL);
	pidns_printf("sched_setscheduler(%d%s, SCHED_FIFO, NULL) = %s\n",
	       pid, pidns_pid2str(PT_TGID), sprintrc(rc));

	rc = syscall(__NR_sched_setscheduler, pid, SCHED_FIFO, param + 1);
	pidns_printf("sched_setscheduler(%d%s, SCHED_FIFO, %p) = %s\n",
	       pid, pidns_pid2str(PT_TGID), param + 1, sprintrc(rc));

	rc = syscall(__NR_sched_setscheduler, pid, 0xfaceda7a, param);
	pidns_printf("sched_setscheduler(%d%s, %#x /* SCHED_??? */, [%d]) = %s\n",
	       pid, pidns_pid2str(PT_TGID), 0xfaceda7a,
	       param->sched_priority, sprintrc(rc));

	rc = syscall(__NR_sched_setscheduler, -1, SCHED_FIFO, param);
	pidns_printf("sched_setscheduler(-1, SCHED_FIFO, [%d]) = %s\n",
	       param->sched_priority, sprintrc(rc));

	rc = syscall(__NR_sched_setscheduler, pid, SCHED_FIFO, param);
	pidns_printf("sched_setscheduler(%d%s, SCHED_FIFO, [%d]) = %s\n",
	       pid, pidns_pid2str(PT_TGID), param->sched_priority, sprintrc(rc));

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_sched_getscheduler && __NR_sched_setscheduler")

#endif
