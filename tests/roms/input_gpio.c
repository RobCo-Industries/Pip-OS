/*
 * Prints every input event it reads, with the held buttons and the dial
 * angle read right after it, for tests/test_input_gpio.py. It skips key
 * repeats: the test holds a line until it sees the event, however long
 * the host takes. The release of the dial push ends the ROM.
 */
#include "rt.h"

int pip_main(const pipos_startup_t *startup)
{
    pip_event_t ev;

    (void)startup;
    rt_begin("gpio_ready");
    rt_dec("buttons", pip_read_buttons());
    rt_dec("dial", pip_read_dial());
    rt_end();
    for (;;) {
        if (pip_wait_event(&ev, PIP_WAIT_FOREVER) != 1) {
            return 1;
        }
        if (ev.type == PIP_EV_BUTTON && ev.value == PIP_EV_REPEAT) {
            continue;
        }
        rt_begin("event");
        rt_dec("type", (int32_t)ev.type);
        rt_dec("code", (int32_t)ev.code);
        rt_dec("value", ev.value);
        rt_dec("buttons", pip_read_buttons());
        rt_dec("dial", pip_read_dial());
        rt_end();
        if (ev.type == PIP_EV_BUTTON && ev.code == PIP_BTN_DIAL && ev.value == PIP_EV_RELEASE) {
            return 0;
        }
    }
}
