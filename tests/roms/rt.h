/*
 * The test ROMs' names for the SDK's self-check markers, pipos/selftest.h
 * in libpip.a. Each ROM prints "@RC stage=testrom ev=<ev> key=value ..."
 * lines that the target tests read with vm.markers().
 *
 * rt_check() and rt_check_eq() print one "ev=check" line each, and
 * rt_checks_done() prints "ev=checks total=<n> failed=<n>". In Python,
 * rom_checks(vm) and assert_rom_checks(vm, names) read them. See
 * docs/TESTING.md.
 */
#ifndef TESTROM_RT_H
#define TESTROM_RT_H

#include <stddef.h>
#include <stdint.h>

#include <pipos/pip.h>
#include <pipos/selftest.h>

#define rt_begin        pip_marker_begin
#define rt_hex          pip_marker_hex
#define rt_dec          pip_marker_dec
#define rt_end          pip_marker_end
#define rt_mark         pip_marker_time
#define rt_check        pip_check
#define rt_check_eq     pip_check_eq
#define rt_checks_done  pip_checks_done

#endif
