/*
 * Pip-OS kernel start-up: hardware, configuration, boot screen, ROM
 * check, the ROM itself, then the main loop.
 */
#include <stdint.h>

#include <robco/alloc.h>
#include <robco/audio.h>
#include <robco/board.h>
#include <robco/boot.h>
#include <robco/console.h>
#include <robco/datetime.h>
#include <robco/fb.h>
#include <robco/gpio.h>
#include <robco/i2c.h>
#include <robco/input.h>
#include <robco/irq.h>
#include <robco/klibc.h>
#include <robco/mmu.h>
#include <robco/rimg.h>
#include <robco/rtc.h>
#include <robco/tick.h>
#include <robco/uart.h>
#include <robco/uart_mini.h>
#include <robco/umode.h>

#include "bootlines.h"
#include "display.h"
#include "pipos.h"
#include "slot_backend.h"
#include "storage.h"
#include "sys.h"

/* State the script command "state" reports. */
static int ready_flag;
static int rom_loaded;
static input_script_reply_t *dump_reply;

static const struct {
    unsigned pin, code;
} buttons[] = {
    { PIPOS_PIN_UP, INPUT_KEY_UP },
    { PIPOS_PIN_DOWN, INPUT_KEY_DOWN },
    { PIPOS_PIN_LEFT, INPUT_KEY_LEFT },
    { PIPOS_PIN_RIGHT, INPUT_KEY_RIGHT },
    { PIPOS_PIN_SELECT, INPUT_KEY_SELECT },
    { PIPOS_PIN_BACK, INPUT_KEY_BACK },
};

static const char *mode_name(uint32_t mode)
{
    switch (mode) {
    case 0x13: return "svc";
    case 0x1a: return "hyp";
    default:   return "other";
    }
}

static void start_display(void)
{
    int rc = mmu_init();

    if (rc != 0) {
        pipos_panic("mmu_init failed");
    }
    mmu_enable();
    k_printf("@RC stage=" PIPOS_STAGE " ev=mmu on=%d\n", mmu_enabled());

    if (fb_init(640, 480, 32) != 0) {
        pipos_panic("framebuffer init failed");
    }
    if (console_init(CONSOLE_SCHEME_GREEN) != 0) {
        pipos_panic("console init failed");
    }
}

static void start_heap(void)
{
    void *base;
    size_t size = robco_heap_slot(&base);

    if (size == 0) {
        pipos_panic("no heap region");
    }
    k_heap_init(base, size);
    k_printf("@RC stage=" PIPOS_STAGE " ev=heap base=0x%08x size=%u\n",
             (unsigned)(uintptr_t)base, (unsigned)size);
}

static void start_tick(void)
{
    irq_init();
    if (tick_init(PIPOS_TICK_HZ) != 0) {
        pipos_panic("tick init failed");
    }
    irq_cpu_enable();
    k_printf("@RC stage=" PIPOS_STAGE " ev=tick hz=%u\n", (unsigned)tick_hz());
}

static void dump_char(char c)
{
    char s[2] = { c, '\0' };

    dump_reply->write(s);
}

/* The umode_request_kill() reason of the kill script command. */
#define PIPOS_KILL_SCRIPT 1

/*
 * Project commands on the script channel: dump, state, rtc, kill,
 * panic <reason>, and tftdump and holo in test builds.
 */
static int script_command(int argc, char *argv[], input_script_reply_t *reply, void *ctx)
{
    static char why[PIPOS_LINE_MAX];
    k_heap_stats_t heap;
    char line[64];
    size_t n = 0;
    int i;

    (void)ctx;
    if (k_strcmp(argv[0], "dump") == 0 && argc == 1) {
        dump_reply = reply;
        console_dump(dump_char);
        return 0;
    }
    if (k_strcmp(argv[0], "state") == 0 && argc == 1) {
        k_heap_stats(&heap);
        k_snprintf(line, sizeof(line), "ready=%d rom=%d heap_free=%u files=%u holo=%d slot=%s\n",
                   ready_flag, rom_loaded, (unsigned)heap.free, pipos_storage_open_count(),
                   pipos_holo_running(), pipos_slot_state_name());
        reply->write(line);
        return 0;
    }
    /* Reads the clock chip itself, not the software clock. */
    if (k_strcmp(argv[0], "rtc") == 0 && argc == 1) {
        datetime_t dt;
        char iso[DT_ISO_SIZE];
        int rc = rtc_present() ? rtc_get(&dt) : RTC_ERR_ABSENT;

        if (rc != 0 || dt_format_iso(&dt, iso, sizeof(iso)) <= 0) {
            k_snprintf(line, sizeof(line), "rc=%d time=none\n", rc);
        } else {
            k_snprintf(line, sizeof(line), "rc=0 time=%s\n", iso);
        }
        reply->write(line);
        return 0;
    }
    /* Ends the innermost running image, as a watchdog would. */
    if (k_strcmp(argv[0], "kill") == 0 && argc == 1) {
        umode_request_kill(PIPOS_KILL_SCRIPT);
        return 0;
    }
    /* Test builds: the panel transcript since tft_init(). */
    if (k_strcmp(argv[0], "tftdump") == 0 && argc == 1 && pipos_display_dump(reply) == 0) {
        return 0;
    }
#ifdef PIPOS_HOLO_TEST
    /* Test builds: drives the RAM slot backend, see slot_test.c. */
    if (k_strcmp(argv[0], "holo") == 0) {
        return pipos_slot_test_command(argc, argv, reply);
    }
#endif
    if (k_strcmp(argv[0], "panic") == 0 && argc >= 2) {
        for (i = 1; i < argc; i++) {
            n += (size_t)k_snprintf(why + n, sizeof(why) - n, "%s%s", i > 1 ? " " : "",
                                    argv[i]);
            if (n >= sizeof(why)) {
                break;
            }
        }
        pipos_panic(why);
    }
    reply->reason = "unknown command";
    return -1;
}

static void start_input(void)
{
    int mini = uart_mini_init(115200);
    int gpio = 0;
    int serial;
    int script = INPUT_ERR_NO_UART;
    int repeat;
    unsigned i;

    /* input_init() sets INPUT_DROP_NEWEST: choose the policy after it. */
    input_init();
    input_set_overflow(INPUT_DROP_OLDEST);
    repeat = input_set_repeat(PIPOS_REPEAT_DELAY_MS, PIPOS_REPEAT_RATE_MS);
    for (i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (gpio == 0) {
            gpio = input_gpio_add_button(buttons[i].pin, buttons[i].code, 0, GPIO_PULL_UP);
        }
    }
    if (gpio == 0) {
        gpio = input_gpio_add_dial(PIPOS_PIN_DIAL_A, PIPOS_PIN_DIAL_B, PIPOS_PIN_DIAL_PUSH,
                                   PIPOS_DIAL_PER_DETENT);
    }
    if (gpio == 0) {
        gpio = input_gpio_start(PIPOS_TICK_HZ, PIPOS_DEBOUNCE_MS);
    }
    serial = input_serial_start(INPUT_SERIAL_MAIN);
    input_script_set_handler(script_command, NULL);
    if (mini == 0) {
        script = input_script_start();
    }
    if (script == 0) {
        uart_mini_puts("ready\n");
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=input gpio=%d serial=%d mini=%d script=%d repeat=%d\n",
             gpio, serial, mini, script, repeat);
}

static void start_audio(void)
{
    int rc = audio_init();

    /*
     * A probe or pin error still leaves the trace backend, which plays every
     * sound. The spec sets PIP_HW_AUDIO when the sound calls can play, so the
     * backend decides, not rc.
     */
    pipos_audio_driver_result(k_strcmp(audio_backend(), "none") != 0);

    k_printf("@RC stage=" PIPOS_STAGE " ev=audio_init rc=%d backend=%s probe=%s\n", rc,
             audio_backend(), audio_probe_name(audio_probe_result()));
}

static void start_clock(void)
{
    datetime_t now;
    char iso[DT_ISO_SIZE];
    int i2c = i2c_init(100000);
    const char *rtc = "absent";

    if (i2c == 0 && rtc_init() == 0) {
        rtc = "present";
    }
    if (clock_get(&now) == 0 && dt_format_iso(&now, iso, sizeof(iso)) > 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=clock rtc=%s time=%s\n", rtc, iso);
    } else {
        k_printf("@RC stage=" PIPOS_STAGE " ev=clock rtc=%s time=unset\n", rtc);
    }
}

static void apply_settings(const pipos_settings_t *s)
{
    console_set_scheme(s->scheme);
    audio_set_volume(s->volume);
    pipos_audio_start_volume(s->volume);
    pipos_screen_delay(s->line_delay_ms);
    if (s->beep) {
        audio_beep();
    }
}

static int boot_screen(int card, loader_result_t *res)
{
    char line[PIPOS_LINE_MAX];
    char name[PIPOS_ROM_NAME_MAX + 1];
    unsigned i;
    int rc;

    for (i = 0; pipos_boot_head[i] != NULL; i++) {
        pipos_screen_line(pipos_boot_head[i]);
    }
    pipos_holo_line(pipos_holo_count(card), line, sizeof(line));
    pipos_screen_line(line);

    rc = pipos_rom_load(card, res);
    if (rc == RIMG_OK) {
        pipos_fmt_name(res->hdr->name, sizeof(res->hdr->name), name, sizeof(name), 1);
        pipos_rom_line(name, res->hdr->image_version, line, sizeof(line));
    } else {
        pipos_rom_error_line(card, rc, line, sizeof(line));
    }
    pipos_screen_line(line);

    for (i = 0; pipos_boot_tail[i] != NULL; i++) {
        pipos_screen_line(pipos_boot_tail[i]);
    }
    return rc == RIMG_OK;
}

static const char *source_name(unsigned source)
{
    switch (source) {
    case INPUT_SRC_GPIO:   return "gpio";
    case INPUT_SRC_SERIAL: return "serial";
    case INPUT_SRC_SCRIPT: return "script";
    default:               return "other";
    }
}

static void report_event(const input_event_t *ev)
{
    const char *name;

    pipos_input_event(ev);
    if (ev->type == INPUT_EV_KEY) {
        name = input_key_name(ev->code);
        k_printf("@RC stage=" PIPOS_STAGE " ev=key type=key src=%s code=%s value=%d\n",
                 source_name(ev->source), name != NULL ? name : "unknown", (int)ev->value);
    } else if (ev->type == INPUT_EV_DIAL) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=key type=dial src=%s code=%u value=%d\n",
                 source_name(ev->source), (unsigned)ev->code, (int)ev->value);
    } else {
        k_printf("@RC stage=" PIPOS_STAGE " ev=key type=char src=%s code=%u value=%d\n",
                 source_name(ev->source), (unsigned)ev->code, (int)ev->value);
    }
}

void pipos_poll(void)
{
    input_event_t ev;

    audio_poll();
    while (input_poll(&ev)) {
        report_event(&ev);
    }
    pipos_sys_poll();
}

static void main_loop(void)
{
    input_event_t ev;

    for (;;) {
        audio_poll();
        pipos_sys_poll();
        if (!input_wait(&ev, 20)) {
            continue;
        }
        report_event(&ev);
    }
}

void robco_main(void)
{
    const robco_board_t *b = board_get();
    pipos_settings_t settings;
    loader_result_t res;
    int card;
    int rom;

    uart_init(115200);
    k_printf("\n@RC stage=" PIPOS_STAGE " ev=boot midr=0x%08x soc=%s base=0x%08x "
             "board_rev=0x%08x mem=0x%08x entry_mode=%s\n",
             (unsigned)b->midr, b->soc_name, (unsigned)b->periph_base,
             (unsigned)b->board_rev, (unsigned)b->mem_size,
             mode_name(robco_boot_args()->entry_mode));

    start_display();
    start_heap();
    start_tick();
    start_input();
    start_audio();
    start_clock();

    card = pipos_card_mount();
    pipos_config_load(card, &settings);
    apply_settings(&settings);
    pipos_display_start(&settings);
    pipos_slot_start(settings.holo_slot);
    pipos_sys_init();

    rom = boot_screen(card, &res);
    rom_loaded = rom;
    if (rom) {
        pipos_rom_run(&res);
    }
    ready_flag = 1;
    k_printf("@RC stage=" PIPOS_STAGE " ev=ready rom=%d\n", rom);
    main_loop();
}
