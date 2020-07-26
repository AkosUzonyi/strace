/*
 * Test PID namespace translation
 *
 * Copyright (c) 2020 Ákos Uzonyi <uzonyi.akos@gmail.com>
 * All rights reserved.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
#ifndef STRACE_PIDNS_H
#define STRACE_PIDNS_H

#ifdef PIDNS_TRANSLATION
# define PIDNS_TEST_INIT pidns_test_init()
#else
# define PIDNS_TEST_INIT
#endif

#include <sys/types.h>

enum pid_type {
	PT_TID,
	PT_TGID,
	PT_PGID,
	PT_SID,

	PT_COUNT,
	PT_NONE = -1
};

/* Prints leader (process tid) if pidns_test_init was called */
void pidns_print_leader(void);

/*
 * Returns a static buffer containing the translation of our PID.
 */
const char *pidns_pid2str(enum pid_type type);

/**
 * Init pidns testing.
 *
 * Should be called at the beginning of the test's main function
 *
 * This function returns from a of child process that is in a new PID namespace.
 */
void pidns_test_init(void);

#endif