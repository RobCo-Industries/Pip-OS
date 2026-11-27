/*
 * conformance/rom/hw_flags.h: the rules the conformance ROM applies to
 * sys_info's hw_flags. QEMU's clock chip cannot hold an invalid time, so
 * that case is covered here.
 */
#include <stdint.h>

#include <pipos/abi.h>

#include "rc_test.h"
#include "../../conformance/rom/hw_flags.h"

TEST(rtc_any_bit_any_clock_state)
{
    /* A chip with a valid time, then one with an invalid time. */
    CHECK_EQ(conform_hw_rtc_ok(PIP_HW_RTC, 0), 1);
    CHECK_EQ(conform_hw_rtc_ok(PIP_HW_RTC, -PIP_ENODEV), 1);
    /* No chip: unset, or set by an earlier image. */
    CHECK_EQ(conform_hw_rtc_ok(0, -PIP_ENODEV), 1);
    CHECK_EQ(conform_hw_rtc_ok(0, 0), 1);
}

TEST(rtc_other_results_fail)
{
    CHECK_EQ(conform_hw_rtc_ok(PIP_HW_RTC, -PIP_EFAULT), 0);
    CHECK_EQ(conform_hw_rtc_ok(0, -PIP_EIO), 0);
    CHECK_EQ(conform_hw_rtc_ok(0, 1), 0);
}

TEST(audio_bit_set_must_play)
{
    CHECK_EQ(conform_hw_audio_ok(PIP_HW_AUDIO, 0, 1), 1);
    CHECK_EQ(conform_hw_audio_ok(PIP_HW_AUDIO, -PIP_EIO, 0), 0);
    CHECK_EQ(conform_hw_audio_ok(PIP_HW_AUDIO, 0, 0), 0);
}

TEST(audio_bit_clear_may_play_or_refuse)
{
    /* The driver failed but a fallback backend still plays the tone. */
    CHECK_EQ(conform_hw_audio_ok(0, 0, 1), 1);
    CHECK_EQ(conform_hw_audio_ok(0, -PIP_EIO, 0), 1);
    CHECK_EQ(conform_hw_audio_ok(0, -PIP_EIO, 1), 0);
    CHECK_EQ(conform_hw_audio_ok(0, 0, 0), 0);
    CHECK_EQ(conform_hw_audio_ok(0, -PIP_EINVAL, 0), 0);
}

TEST(reserved_bits)
{
    CHECK_EQ(conform_hw_reserved_ok(PIP_HW_RTC | PIP_HW_AUDIO), 1);
    CHECK_EQ(conform_hw_reserved_ok(0), 1);
    CHECK_EQ(conform_hw_reserved_ok(1u << 1), 0);
    CHECK_EQ(conform_hw_reserved_ok(1u << 31), 0);
    CHECK_EQ(conform_hw_reserved_ok(PIP_HW_TAPE), 1);
    CHECK_EQ(conform_hw_reserved_ok(PIP_HW_AUDIO_MID), 1);
    CHECK_EQ(conform_hw_reserved_ok(1u << 5), 0);
}

TEST(audio_mid)
{
    CHECK_EQ(conform_hw_audio_mid_ok(0), 1);
    CHECK_EQ(conform_hw_audio_mid_ok(PIP_HW_AUDIO), 1);
    CHECK_EQ(conform_hw_audio_mid_ok(PIP_HW_AUDIO | PIP_HW_AUDIO_MID), 1);
    CHECK_EQ(conform_hw_audio_mid_ok(PIP_HW_AUDIO_MID), 0);
    CHECK_EQ(conform_hw_audio_mid_ok(PIP_HW_RTC | PIP_HW_AUDIO_MID), 0);
}

TEST(holo_flags)
{
    CHECK_EQ(conform_holo_flags_ok(0, 0), 1);
    CHECK_EQ(conform_holo_flags_ok(PIP_HOLO_SLOT, PIP_HW_TAPE), 1);
    CHECK_EQ(conform_holo_flags_ok(PIP_HOLO_SLOT, 0), 0);
    CHECK_EQ(conform_holo_flags_ok(2, PIP_HW_TAPE), 0);
}

TEST(eject_mask)
{
    CHECK_EQ(conform_eject_mask_ok(0, 0), 1);
    CHECK_EQ(conform_eject_mask_ok(1, 0), 0);
    CHECK_EQ(conform_eject_mask_ok(7, PIP_HW_TAPE), 1);
    CHECK_EQ(conform_eject_mask_ok(8, PIP_HW_TAPE), 0);
    CHECK_EQ(conform_eject_mask_ok(-1, PIP_HW_TAPE), 0);
}
