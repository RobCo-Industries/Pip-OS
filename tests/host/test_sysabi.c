#include <limits.h>
#include <stddef.h>
#include <string.h>

#include <pipos/abi.h>
#include <robco/exc.h>

#include "rc_test.h"
#include "sysabi.h"

TEST(abi_values)
{
    CHECK_EQ(PIP_ABI_VERSION, 0x00010006);
    CHECK_EQ(PIP_HW_RTC, 0x1u);
    CHECK_EQ(PIP_HW_AUDIO, 0x4u);
    CHECK_EQ(PIP_SYS_SAMPLE_QUEUE, 0x26);
    CHECK_EQ(PIP_SYS_SAMPLE_RATE, 0x27);
    CHECK_EQ(PIP_ROM_HEAP_SIZE, 0x007F0000);
    CHECK_EQ(PIPOS_ROM_STARTUP, 0x017F0000u);
    CHECK_EQ(PIPOS_ROM_STUB, 0x017F0080u);
    CHECK_EQ(PIP_SYS_TICKS_MS, 0x42);
    CHECK_EQ(PIP_SYS_RAND, 0x49);
    CHECK_EQ(PIP_SYS_HOLO_EJECT_EVENT, 0x63);
    CHECK_EQ(PIP_EFAULT, 14);
    CHECK_EQ(PIP_EINVAL, 22);
    CHECK_EQ(PIP_ENOSYS, 38);
}

TEST(version_pack)
{
    CHECK_EQ(pipos_version_pack(7, 1, 0), 0x00070100u);
    CHECK_EQ(pipos_version_pack(1, 2, 10), 0x0001020Au);
    CHECK_EQ(pipos_version_pack(0x1ffff, 0x1ff, 0x1ff), 0xFFFFFFFFu);
}

TEST(startup_block)
{
    pipos_startup_t s;
    unsigned i;

    memset(&s, 0xaa, sizeof(s));
    pipos_startup_rom(&s);
    CHECK_EQ(s.abi_version, 0x00010006u);
    CHECK_EQ(s.size, 128u);
    CHECK_EQ(s.heap_base, 0x01000000u);
    CHECK_EQ(s.heap_size, 0x007F0000u);
    CHECK_EQ(s.stack_top, 0x01800000u);
    CHECK_EQ(s.kind, 0u);
    CHECK_EQ(s.flags, 0u);
    for (i = 0; i < sizeof(s.args); i++) {
        CHECK_EQ(s.args[i], 0);
    }
}

TEST(info_structs)
{
    pip_mem_info_t m;
    pip_sys_info_t s;

    pipos_mem_info_rom(&m);
    CHECK_EQ(m.heap_base, 0x01000000u);
    CHECK_EQ(m.heap_size, 0x007F0000u);
    CHECK_EQ(m.stack_top, 0x01800000u);
    CHECK_EQ(m.stack_size, 0x00010000u);

    memset(&s, 0xaa, sizeof(s));
    pipos_sys_info_fill(&s, 0x00070100u, 0x9000c1u, 0x2835u, 0x1c000000u, PIP_HW_AUDIO);
    CHECK_EQ(s.size, 32u);
    CHECK_EQ(s.abi_version, 0x00010006u);
    CHECK_EQ(s.pipos_version, 0x00070100u);
    CHECK_EQ(s.board_rev, 0x9000c1u);
    CHECK_EQ(s.soc, 0x2835u);
    CHECK_EQ(s.mem_size, 0x1c000000u);
    CHECK_EQ(s.hw_flags, PIP_HW_AUDIO);
    CHECK_EQ(s.reserved, 0u);
}

TEST(hw_flags)
{
    CHECK_EQ(pipos_hw_flags(0, 0, 0, 0), 0u);
    CHECK_EQ(pipos_hw_flags(1, 0, 0, 0), PIP_HW_RTC);
    CHECK_EQ(pipos_hw_flags(0, 1, 0, 0), PIP_HW_AUDIO);
    CHECK_EQ(pipos_hw_flags(1, 1, 0, 0), PIP_HW_RTC | PIP_HW_AUDIO);
    /* Any non-zero answer sets only its own bit: bit 1 and bits 5 to 31 stay 0. */
    CHECK_EQ(pipos_hw_flags(-1, 7, 0, 0), 0x5u);
    CHECK_EQ(pipos_hw_flags(0, 0, 1, 0), PIP_HW_TAPE);
    CHECK_EQ(pipos_hw_flags(1, 1, 1, 0), 0xDu);
    CHECK_EQ(pipos_hw_flags(1, 1, 1, 9), 0x1Du);
}

/*
 * PIP_HW_AUDIO_MID, ABI 1.5. main.c passes 1 only when system_sound is on
 * and the rest ramp returned 0 and its wait ended. Sound off and a failed
 * ramp pass 0. Without PIP_HW_AUDIO the bit stays clear.
 */
TEST(hw_flags_audio_mid)
{
    CHECK_EQ(pipos_hw_flags(0, 1, 0, 1), PIP_HW_AUDIO | PIP_HW_AUDIO_MID);
    CHECK_EQ(pipos_hw_flags(0, 1, 0, 0), PIP_HW_AUDIO);
    CHECK_EQ(pipos_hw_flags(0, 0, 0, 1), 0u);
    CHECK_EQ(pipos_hw_flags(1, 0, 1, 1), PIP_HW_RTC | PIP_HW_TAPE);
}

TEST(log_char)
{
    CHECK_EQ(pipos_log_char('a'), 'a');
    CHECK_EQ(pipos_log_char(' '), ' ');
    CHECK_EQ(pipos_log_char('~'), '~');
    CHECK_EQ(pipos_log_char('\n'), '\n');
    CHECK_EQ(pipos_log_char('\t'), '\t');
    CHECK_EQ(pipos_log_char('\r'), '?');
    CHECK_EQ(pipos_log_char('\x01'), '?');
    CHECK_EQ(pipos_log_char('\x1b'), '?');
    CHECK_EQ(pipos_log_char('\x7f'), '?');
    CHECK_EQ(pipos_log_char((char)0x80), '?');
    CHECK_EQ(pipos_log_char((char)0xff), '?');
}

TEST(str_check)
{
    CHECK_EQ(pipos_str_check(0, 1), 0);
    CHECK_EQ(pipos_str_check(255, 0), 255);
    CHECK_EQ(pipos_str_check(-1, 1), -PIP_EINVAL);
    CHECK_EQ(pipos_str_check(-1, 0), -PIP_EFAULT);
}

TEST(fault_names)
{
    char buf[64];

    CHECK_STR(pipos_fault_reason(ROBCO_EXC_UNDEF), "undef");
    CHECK_STR(pipos_fault_reason(ROBCO_EXC_PABT), "prefetch_abort");
    CHECK_STR(pipos_fault_reason(ROBCO_EXC_DABT), "data_abort");
    CHECK_STR(pipos_fault_reason(ROBCO_EXC_SVC), "thumb_svc");
    CHECK_STR(pipos_fault_reason(ROBCO_EXC_IRQ), "other");

    pipos_fault_line(ROBCO_EXC_DABT, 0x004003a8u, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) DATA ABORT AT 0x004003A8");
    pipos_fault_line(ROBCO_EXC_PABT, 0x8000u, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) PREFETCH ABORT AT 0x00008000");
    pipos_fault_line(ROBCO_EXC_UNDEF, 0xffffffffu, buf, sizeof(buf));
    CHECK_STR(buf, "ROM(1) UNDEFINED INSTRUCTION AT 0xFFFFFFFF");
}

TEST(sleep_check)
{
    CHECK_EQ(pipos_sleep_check(0), 0);
    CHECK_EQ(pipos_sleep_check(1000), 0);
    CHECK_EQ(pipos_sleep_check(0x7fffffffu), 0);
    CHECK_EQ(pipos_sleep_check(0x80000000u), -PIP_ERANGE);
    CHECK_EQ(pipos_sleep_check(0xffffffffu), -PIP_ERANGE);
}

TEST(sleep_deadline)
{
    CHECK_EQ(pipos_sleep_deadline_us(0, 0), 0);
    CHECK_EQ(pipos_sleep_deadline_us(5, 1), 1005);
    CHECK_EQ(pipos_sleep_deadline_us(123456789, 13), 123469789);
    /* No 32-bit overflow: the largest sleep is about 24.8 days. */
    CHECK_EQ(pipos_sleep_deadline_us(0, 0x7fffffffu), 0x7fffffffull * 1000u);
    CHECK_EQ(pipos_sleep_deadline_us(0xffffffffull, 2), 0xffffffffull + 2000u);
}

TEST(exit_status)
{
    CHECK_EQ(pipos_exit_status(0), 0);
    CHECK_EQ(pipos_exit_status(255), 255);
    CHECK_EQ(pipos_exit_status(256), 0);
    CHECK_EQ(pipos_exit_status(300), 44);
    CHECK_EQ(pipos_exit_status(-1), 255);
    CHECK_EQ(pipos_exit_status(INT_MIN), 0);
}
