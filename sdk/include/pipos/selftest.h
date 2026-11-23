/*
 * Self-checks for a ROM or a holotape under test: marker lines through
 * the log call that the target tests read.
 *
 * Each line is "@RC stage=testrom ev=<ev> key=value ...". pip_check()
 * and pip_check_eq() print one "ev=check name=<name> ok=<0|1>
 * [got=<n> expected=<n>]" line each, and pip_checks_done() prints
 * "ev=checks total=<n> failed=<n>". In Python, rom_checks(vm) and
 * assert_rom_checks(vm, names) from the pipos_testing package read them.
 * See docs/TESTING.md.
 *
 * One line is built at a time, in a buffer of PIP_LOG_MAX bytes: a
 * longer line is cut. The counters are global, so call these from one
 * thread of control.
 */
#ifndef PIPOS_SELFTEST_H
#define PIPOS_SELFTEST_H

#include <stdint.h>

/* Starts a marker line: "@RC stage=testrom ev=<ev>". */
void pip_marker_begin(const char *ev);
/* Appends " <key>=0x<8 hex digits>". */
void pip_marker_hex(const char *key, uint32_t v);
/* Appends " <key>=<signed decimal>". */
void pip_marker_dec(const char *key, int32_t v);
/* Ends the line and logs it in one call. */
void pip_marker_end(void);

/* Prints "ev=<ev> t_ms=<pip_ticks_ms()>": the kernel's time right now. */
void pip_marker_time(const char *ev);

/* One named check. name: no spaces, at most 64 bytes. Returns ok. */
int pip_check(const char *name, int ok);
/* A check that got == expected, with both values in the line. */
int pip_check_eq(const char *name, int32_t got, int32_t expected);
/* The summary line. Returns the number of failed checks. */
int pip_checks_done(void);

#endif
