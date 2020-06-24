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

extern pid_t pidns_ids[PT_COUNT];
extern pid_t pidns_strace_ids[PT_COUNT];

void pidns_print_leader(void);
void pidns_print_id_pair(enum pid_type type);
void pidns_test_init(void);

#endif