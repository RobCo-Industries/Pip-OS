/*
 * Small runtime for the test ROMs: marker lines through the log call.
 * Each ROM prints "@RC stage=testrom ev=<ev> key=value ..." lines that
 * the target tests read with vm.markers(). The rest comes from libpip.a.
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

#endif
