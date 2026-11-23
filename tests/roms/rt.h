/*
 * Small runtime for the test ROMs: marker lines through the log call.
 * Each ROM prints "@RC stage=testrom ev=<ev> key=value ..." lines that
 * the target tests read with vm.markers(). The rest comes from libpip.a.
 *
 * Named checks: rt_check() and rt_check_eq() print one
 * "ev=check name=<name> ok=<0|1> [got=<n> expected=<n>]" line each, and
 * rt_checks_done() prints "ev=checks total=<n> failed=<n>". In Python,
 * rom_checks(vm) from tests/pipos_helpers.py returns them as a list and
 * assert_rom_checks(vm, names) checks them. See docs/TESTING.md.
 */
#ifndef TESTROM_RT_H
#define TESTROM_RT_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/pip.h>

/* Starts a marker line: "@RC stage=testrom ev=<ev>". */
void rt_begin(const char *ev);
/* Appends " <key>=0x<8 hex digits>". */
void rt_hex(const char *key, uint32_t v);
/* Appends " <key>=<signed decimal>". */
void rt_dec(const char *key, int32_t v);
/* Ends the line and logs it in one call. */
void rt_end(void);

/*
 * Prints "ev=<ev> t_ms=<pip_ticks_ms()>": the kernel's time right before
 * a call. The audio driver's t_ms counts from the same tick clock.
 */
void rt_mark(const char *ev);

/* One named check. name: no spaces, at most 64 bytes. Returns ok. */
int rt_check(const char *name, int ok);
/* A check that got == expected, with both values in the line. */
int rt_check_eq(const char *name, int32_t got, int32_t expected);
/* The summary line. Returns the number of failed checks. */
int rt_checks_done(void);

#endif
