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
#include "sys.h"
#include "sysabi.h"

/* The only memory the ROM can touch, both read-write. */
static const umode_region_t rom_regions[] = {
    { PIP_ROM_BASE, PIP_ROM_SIZE, UMODE_REGION_WRITE },
    { PIP_ROM_HEAP_BASE, PIP_ROM_HEAP_REGION, UMODE_REGION_WRITE },
};

static void report_fault(const umode_exit_t *ex)
{
    char line[PIPOS_LINE_MAX];

    k_printf("@RC stage=" PIPOS_STAGE " ev=rom_kill reason=%s pc=0x%08x",
             pipos_fault_reason(ex->type), (unsigned)ex->pc);
    if (ex->type == ROBCO_EXC_DABT) {
        k_printf(" dfar=0x%08x dfsr=0x%08x", (unsigned)ex->addr, (unsigned)ex->status);
    } else if (ex->type == ROBCO_EXC_PABT) {
        k_printf(" ifar=0x%08x ifsr=0x%08x", (unsigned)ex->addr, (unsigned)ex->status);
    }
    k_printf("\n");
    pipos_fault_line(ex->type, ex->pc, line, sizeof(line));
    pipos_screen_line(line);
}

void pipos_rom_run(const loader_result_t *res)
{
    static umode_exit_t ex;
    pipos_startup_t *startup = (pipos_startup_t *)(uintptr_t)PIPOS_ROM_STARTUP;
    umode_task_t task;
    int rc;

    /* The regions are privileged RAM until umode_run() maps them. */
    pipos_startup_rom(startup);
    cache_clean_range((const void *)(uintptr_t)PIP_ROM_BASE, res->file_size);

    k_memset(&task, 0, sizeof(task));
    task.entry = PIP_ROM_BASE;
    task.sp = PIP_ROM_STACK_TOP;
    task.r[0] = PIPOS_ROM_STARTUP;
    task.stub = PIPOS_ROM_STUB;
    task.regions = rom_regions;
    task.region_count = sizeof(rom_regions) / sizeof(rom_regions[0]);

    pipos_sys_image_start(PIP_KIND_ROM);
    k_printf("@RC stage=" PIPOS_STAGE " ev=jump entry=0x%08x\n", (unsigned)task.entry);
    rc = umode_run(&task, &ex);
    pipos_sys_image_end(PIP_KIND_ROM);
    if (rc != 0) {
        k_printf("@RC stage=" PIPOS_STAGE " ev=error code=umode rc=%d\n", rc);
        return;
    }
    switch (ex.reason) {
    case UMODE_RETURNED:
    case UMODE_EXITED:
        k_printf("@RC stage=" PIPOS_STAGE " ev=rom_exit status=%d\n", ex.code);
        break;
    case UMODE_FAULT:
        report_fault(&ex);
        break;
    default:
        /* Pip-OS requests no kill yet. */
        k_printf("@RC stage=" PIPOS_STAGE " ev=rom_kill reason=killed code=%d pc=0x%08x\n",
                 ex.code, (unsigned)ex.pc);
        break;
    }
}
