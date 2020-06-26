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

/* Prints leader (process tid) before printf */
void pidns_printf(const char *format, ...);

/*
 * Returns a static buffer containing the translation of our PID
 */
const char *pidns_pid2str(enum pid_type type);

/*
 * Init pidns testing, when strace is run without the -Y flag
 * Should be called at the beginning of the test's main function
 */
void pidns_test_init(void);

/*
 * Init pidns testing, when strace is run with the -Y flag
 * Should be called at the beginning of the test's main function
 */
void pidns_test_init_Y(void);

#endif