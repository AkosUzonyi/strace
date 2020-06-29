/*
 * Copyright (c) 2017-2019 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef PIDNS_TEST_INIT
# define PIDNS_TEST_INIT pidns_test_init();
#endif

#include "tests.h"
#include <stdio.h>
#include <unistd.h>
#include "scno.h"
#include "pidns.h"

int
main(void)
{
	PIDNS_TEST_INIT;

	pidns_printf("gettid() = %d%s\n", syscall(__NR_gettid), pidns_pid2str(PT_TID));
	pidns_printf("+++ exited with 0 +++\n");
	return 0;
}
