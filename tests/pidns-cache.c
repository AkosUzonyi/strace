/*
 * Copyright (c) 2020 The strace developers.
 * All rights reserved.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "tests.h"
#include "pidns.h"

#include <stdio.h>
#include <unistd.h>

/**
 * There is a 1 sec timeout for this test. With pidns caching more than 1000
 * syscalls can be executed, without caching only a few tens.
 */
#define SYSCALL_COUNT 200

int
main(void)
{
	pidns_test_init();

        for (int i = 0; i < SYSCALL_COUNT; i++)
                getpid();

	return 0;
}
