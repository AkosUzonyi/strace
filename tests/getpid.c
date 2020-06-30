/*
 * Copyright (c) 2017-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "scno.h"
#include "pidns.h"

#if defined __NR_getpid && (!defined __NR_getxpid || __NR_getxpid != __NR_getpid)

# include <stdio.h>
# include <unistd.h>

int
main(void)
{
#ifdef PIDNS_TRANSLATION
	pidns_test_init();
#endif

	pidns_printf("getpid() = %d%s\n", syscall(__NR_getpid), pidns_pid2str(PT_TGID));
	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_getpid")

#endif
