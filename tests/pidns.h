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

#include <sys/types.h>

enum pid_type {
	PT_TID,
	PT_TGID,
	PT_PGID,
	PT_SID,

	PT_COUNT,
	PT_NONE = -1
};

/* Our PIDs in our namespace */
extern pid_t pidns_ids[PT_COUNT];
/* Our PIDs in strace's namespace */
extern pid_t pidns_strace_ids[PT_COUNT];

/* Prints leader (process tid) before printf */
void pidns_printf(const char *format, ...);

/*
 * Returns a static buffer containing the string that is expected to be printed
 * by strace when printing our PID
 */
const char *pidns_pid2str(enum pid_type type);

/* Should be called at the beginning of the test's main function */
void pidns_test_init(void);

#endif