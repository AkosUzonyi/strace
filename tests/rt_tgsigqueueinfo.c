/*
 * This file is part of rt_tgsigqueueinfo strace test.
 *
 * Copyright (c) 2016 Dmitry V. Levin <ldv@altlinux.org>
 * Copyright (c) 2016-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "scno.h"
#include "pidns.h"

#if defined __NR_rt_tgsigqueueinfo && defined __NR_gettid

# include <errno.h>
# include <signal.h>
# include <stdio.h>
# include <string.h>
# include <unistd.h>

static long
k_tgsigqueueinfo(const pid_t tgid, const int tid, const int sig, const void *const info)
{
	return syscall(__NR_rt_tgsigqueueinfo,
		       F8ILL_KULONG_MASK | tgid,
		       F8ILL_KULONG_MASK | tid,
		       F8ILL_KULONG_MASK | sig,
		       info);
}

int
main(void)
{
#ifdef PIDNS_TRANSLATION
	pidns_test_init();
#endif

	const struct sigaction sa = {
		.sa_handler = SIG_IGN
	};
	if (sigaction(SIGUSR1, &sa, NULL))
		perror_msg_and_fail("sigaction");

	TAIL_ALLOC_OBJECT_CONST_PTR(siginfo_t, info);
	memset(info, 0, sizeof(*info));
	info->si_signo = SIGUSR1;
	info->si_errno = ENOENT;
	info->si_code = SI_QUEUE;
	info->si_pid = getpid();
	info->si_uid = getuid();
	info->si_value.sival_ptr =
		(void *) (unsigned long) 0xdeadbeeffacefeedULL;

	if (k_tgsigqueueinfo(getpid(), syscall(__NR_gettid), SIGUSR1, info))
		(errno == ENOSYS ? perror_msg_and_skip : perror_msg_and_fail)(
			"rt_tgsigqueueinfo");

	pidns_printf("rt_tgsigqueueinfo(%d%s, %d%s, %s, {si_signo=%s"
		", si_code=SI_QUEUE, si_errno=ENOENT, si_pid=%d%s"
		", si_uid=%u, si_value={int=%d, ptr=%p}}) = 0\n",
		info->si_pid, pidns_pid2str(PT_TGID),
		info->si_pid, pidns_pid2str(PT_TID),
		"SIGUSR1", "SIGUSR1",
		info->si_pid, pidns_pid2str(PT_TGID),
		info->si_uid, info->si_value.sival_int,
		info->si_value.sival_ptr);

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_rt_tgsigqueueinfo")

#endif
