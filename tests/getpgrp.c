/*
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

#ifdef __NR_getpgrp

# include <stdio.h>
# include <unistd.h>

int
main(void)
{
	PIDNS_TEST_INIT;

	pidns_printf("getpgrp() = %d%s\n", syscall(__NR_getpgrp), pidns_pid2str(PT_PGID));

	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}

#else

SKIP_MAIN_UNDEFINED("__NR_getpgrp")

#endif
