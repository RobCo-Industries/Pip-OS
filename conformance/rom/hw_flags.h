/*
 * The conformance ROM's sys_info hw_flags rules, ABI 1.3 and 1.4. Pure functions,
 * so tests/host/test_conform_hw.c runs them on the host. Each returns 1
 * when the state conforms to the spec, else 0.
 */
#ifndef CONFORM_HW_FLAGS_H
#define CONFORM_HW_FLAGS_H

#include <stdint.h>

#include <pipos/abi.h>

/*
 * PIP_HW_RTC says a clock chip answered at boot. It says nothing about the
 * time the chip holds. So no rule links the bit to get_time:
 * - bit set, valid time in the chip: get_time returns 0.
 * - bit set, invalid time in the chip: nothing seeds the clock, so
 *   get_time returns -PIP_ENODEV until set_time.
 * - bit clear: get_time returns -PIP_ENODEV, or 0 after set_time.
 * Either way get_time returns 0 or -PIP_ENODEV, and only that is checked.
 */
static inline int conform_hw_rtc_ok(uint32_t hw_flags, int32_t get_time_rc)
{
    (void)hw_flags;
    return get_time_rc == 0 || get_time_rc == -PIP_ENODEV;
}

/*
 * PIP_HW_AUDIO says the audio driver started at boot, so the sound calls
 * can play. With the bit set, a valid tone returns 0 and audio_status
 * reads 1 right after it. With the bit clear the spec does not say the
 * calls fail. A tone may play, then audio_status reads 1. It may be
 * refused, then it returns -PIP_EIO and audio_status reads 0.
 */
static inline int conform_hw_audio_ok(uint32_t hw_flags, int32_t tone_rc, int32_t status)
{
    int played = tone_rc == 0 && status == 1;
    int refused = tone_rc == -PIP_EIO && status == 0;

    return (hw_flags & PIP_HW_AUDIO) ? played : played || refused;
}

/* Bit 1 and bits 4 to 31 are 0. Bit 3 is PIP_HW_TAPE, ABI 1.4. */
static inline int conform_hw_reserved_ok(uint32_t hw_flags)
{
    return (hw_flags & ~(PIP_HW_RTC | PIP_HW_AUDIO | PIP_HW_TAPE)) == 0;
}

/*
 * pip_holo_info_t.flags, ABI 1.4: bits 1 to 15 are 0, and PIP_HOLO_SLOT
 * only when a slot exists.
 */
static inline int conform_holo_flags_ok(uint16_t flags, uint32_t hw_flags)
{
    if ((flags & ~PIP_HOLO_SLOT) != 0) {
        return 0;
    }
    return (flags & PIP_HOLO_SLOT) == 0 || (hw_flags & PIP_HW_TAPE) != 0;
}

/*
 * holo_eject_event, ABI 1.4: 0 with PIP_HW_TAPE clear, else only the
 * PIP_HOLO_EV_* bits.
 */
static inline int conform_eject_mask_ok(int32_t mask, uint32_t hw_flags)
{
    uint32_t all = PIP_HOLO_EV_INSERTED | PIP_HOLO_EV_REMOVED | PIP_HOLO_EV_BAD;

    if ((hw_flags & PIP_HW_TAPE) == 0) {
        return mask == 0;
    }
    return mask >= 0 && ((uint32_t)mask & ~all) == 0;
}

#endif
