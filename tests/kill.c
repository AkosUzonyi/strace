/*
 * Check decoding of kill syscall.
 *
 * Copyright (c) 2015-2016 Dmitry V. Levin <ldv@altlinux.org>
 * Copyright (c) 2016 Fei Jie <feij.fnst@cn.fujitsu.com>
 * Copyright (c) 2016-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PIDNS_TEST_INIT
# define PIDNS_TEST_INIT
#endif

#include "tests.h"
#include "scno.h"
#include "pidns.h"

#ifdef __NR_kill

# include <signal.h>
# include <stdio.h>
# include <unistd.h>

static void
handler(int sig)
{
}

int
main(void)
{
	PIDNS_TEST_INIT;

	const struct sigaction act = { .sa_handler = handler };
	if (sigaction(SIGALRM, &act, NULL))
		perror_msg_and_fail("sigaction");

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGALRM);
	if (sigprocmask(SIG_UNBLOCK, &mask, NULL))
		perror_msg_and_fail("sigprocmask");

	const int pid = getpid();
	long rc = syscall(__NR_kill, pid, (long) 0xdefaced00000000ULL | SIGALRM);
	pidns_printf("kill(%d%s, SIGALRM) = %ld\n", pid, pidns_pid2str(PT_TGID), rc);

	const long big_pid = (long) 0xfacefeedbadc0dedULL;
	const long big_sig = (long) 0xdeadbeefcafef00dULL;
	rc = syscall(__NR_kill, big_pid, big_sig);
	pidns_printf("kill(%d, %d) = %ld %s (%m)\n",
	       (int) big_pid, (int) big_sig, rc, errno2name());

	rc = syscall(__NR_kill, (long) 0xdefaced00000000ULL | pid, 0);
	pidns_printf("kill(%d%s, 0) = %ld\n", pid, pidns_pid2str(PT_TGID), rc);

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_kill")

#endif
