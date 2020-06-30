/*
 * Copyright (c) 2016-2018 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "pidns.h"

#include <stdio.h>
#include <unistd.h>

int
main(void)
{
#ifdef PIDNS_TRANSLATION
	pidns_test_init();
#endif

	pid_t pid = getpid();
	pidns_printf("getsid(%d%s) = %d%s\n", pid, pidns_pid2str(PT_TGID),
		getsid(pid), pidns_pid2str(PT_SID));

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}
