/*
 * Runs the loaded ROM in user mode with RobCo-Core's umode.h, then
 * reports how it ended. The system calls are in syscall.c and sys_*.c.
 */
#include <robco/cache.h>
#include <robco/console.h>
#include <robco/klibc.h>
#include <robco/umode.h>

#include "bootlines.h"
#include "pipos.h"
#include "rom.h"
#include "sys.h"
#include "sysabi.h"

/* The only memory the ROM can touch, both read-write. */
static const umode_region_t rom_regions[] = {
    { PIP_ROM_BASE, PIP_ROM_SIZE, UMODE_REGION_WRITE },
    { PIP_ROM_HEAP_BASE, PIP_ROM_HEAP_REGION, UMODE_REGION_WRITE },
};

void pipos_image_report_kill(const char *who, const umode_exit_t *ex, int screen)
{
    char line[PIPOS_LINE_MAX];

    if (ex->reason != UMODE_FAULT) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=%s_kill reason=killed code=%d pc=0x%08x\n",
                 who, ex->code, (unsigned)ex->pc);
        return;
    }
    k_printf("@RC stage=" PIPOS_STAGE " ev=%s_kill reason=%s pc=0x%08x", who,
             pipos_fault_reason(ex->type), (unsigned)ex->pc);
    if (ex->type == ROBCO_EXC_DABT) {
        k_printf(" dfar=0x%08x dfsr=0x%08x", (unsigned)ex->addr, (unsigned)ex->status);
    } else if (ex->type == ROBCO_EXC_PABT) {
        k_printf(" ifar=0x%08x ifsr=0x%08x", (unsigned)ex->addr, (unsigned)ex->status);
    }
    k_printf("\n");
    if (screen) {
        pipos_fault_line(ex->type, ex->pc, line, sizeof(line));
        pipos_screen_line(line);
    }
}

int pipos_image_run(const pipos_image_t *im, umode_exit_t *ex)
{
    umode_task_t task;
    int rc;

    /* The regions are privileged RAM until umode_run() maps them. */
    cache_clean_range((const void *)(uintptr_t)im->entry, im->load_size);

    k_memset(&task, 0, sizeof(task));
    task.entry = im->entry;
    task.sp = im->sp;
    task.r[0] = im->startup;
    task.stub = im->stub;
    task.regions = im->regions;
    task.region_count = im->region_count;

    pipos_sys_image_start(im->kind);
    k_printf("@RC stage=" PIPOS_STAGE " %s\n", im->start_line);
    rc = umode_run(&task, ex);
    pipos_sys_image_end(im->kind);
    return rc;
}

void pipos_rom_run(const loader_result_t *res)
{
    static umode_exit_t ex;
    static char start[48];
    pipos_image_t im;
    int rc;

    pipos_startup_rom((pipos_startup_t *)(uintptr_t)PIPOS_ROM_STARTUP);
    k_snprintf(start, sizeof(start), "ev=jump entry=0x%08x", (unsigned)PIP_ROM_BASE);
    im.kind = PIP_KIND_ROM;
    im.entry = PIP_ROM_BASE;
    im.sp = PIP_ROM_STACK_TOP;
    im.startup = PIPOS_ROM_STARTUP;
    im.stub = PIPOS_ROM_STUB;
    im.load_size = res->file_size;
    im.regions = rom_regions;
    im.region_count = sizeof(rom_regions) / sizeof(rom_regions[0]);
    im.start_line = start;

    rc = pipos_image_run(&im, &ex);
    pipos_screen_restore(); /* the boot text comes back, however the ROM ended */
    if (rc != 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=error code=umode rc=%d\n", rc);
        return;
    }
    switch (ex.reason) {
    case UMODE_RETURNED:
    case UMODE_EXITED:
        k_printf("@RC stage=" PIPOS_STAGE " ev=rom_exit status=%d\n",
                 (int)pipos_exit_status(ex.code));
        break;
    default:
        pipos_image_report_kill("rom", &ex, ex.reason == UMODE_FAULT);
        break;
    }
}
