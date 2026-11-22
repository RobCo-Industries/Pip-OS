#include <robco/exc.h>
#include <robco/klibc.h>

#include "sysabi.h"

uint32_t pipos_version_pack(unsigned major, unsigned minor, unsigned patch)
{
    return ((uint32_t)(major & 0xffffu) << 16) | ((uint32_t)(minor & 0xffu) << 8) |
           (uint32_t)(patch & 0xffu);
}

int pipos_sys_slot(uint32_t nr)
{
    if (nr < PIPOS_SYS_FIRST || nr - PIPOS_SYS_FIRST >= PIPOS_SYS_SLOTS ||
        nr == PIP_SYS_POWER) {
        return -1;
    }
    return (int)(nr - PIPOS_SYS_FIRST);
}

void pipos_startup_rom(pipos_startup_t *s)
{
    k_memset(s, 0, sizeof(*s));
    s->abi_version = PIP_ABI_VERSION;
    s->size = sizeof(*s);
    s->heap_base = PIP_ROM_HEAP_BASE;
    s->heap_size = PIP_ROM_HEAP_SIZE;
    s->stack_top = PIP_ROM_STACK_TOP;
    s->kind = PIP_KIND_ROM;
}

void pipos_mem_info_rom(pip_mem_info_t *m)
{
    m->heap_base = PIP_ROM_HEAP_BASE;
    m->heap_size = PIP_ROM_HEAP_SIZE;
    m->stack_top = PIP_ROM_STACK_TOP;
    m->stack_size = PIP_STACK_SIZE;
}

void pipos_sys_info_fill(pip_sys_info_t *s, uint32_t pipos_version, uint32_t board_rev,
                         uint32_t soc, uint32_t mem_size)
{
    k_memset(s, 0, sizeof(*s));
    s->size = sizeof(*s);
    s->abi_version = PIP_ABI_VERSION;
    s->pipos_version = pipos_version;
    s->board_rev = board_rev;
    s->soc = soc;
    s->mem_size = mem_size;
}

char pipos_log_char(char c)
{
    unsigned char u = (unsigned char)c;

    if ((u >= 0x20 && u < 0x7f) || c == '\n' || c == '\t') {
        return c;
    }
    return '?';
}

long pipos_log_check(long strnlen_result, int whole_range_ok)
{
    if (strnlen_result >= 0) {
        return strnlen_result;
    }
    /* All 256 bytes were readable, so no NUL; otherwise a byte was outside. */
    return whole_range_ok ? -PIP_EINVAL : -PIP_EFAULT;
}

int32_t pipos_sleep_check(uint32_t ms)
{
    return ms > 0x7fffffffu ? -PIP_ERANGE : 0;
}

uint64_t pipos_sleep_deadline_us(uint64_t now_us, uint32_t ms)
{
    return now_us + (uint64_t)ms * 1000u;
}

const char *pipos_fault_reason(unsigned type)
{
    switch (type) {
    case ROBCO_EXC_UNDEF: return "undef";
    case ROBCO_EXC_PABT:  return "prefetch_abort";
    case ROBCO_EXC_DABT:  return "data_abort";
    case ROBCO_EXC_SVC:   return "thumb_svc";
    default:              return "other";
    }
}

int pipos_fault_line(unsigned type, uint32_t pc, char *buf, size_t size)
{
    const char *what;

    switch (type) {
    case ROBCO_EXC_UNDEF: what = "UNDEFINED INSTRUCTION"; break;
    case ROBCO_EXC_PABT:  what = "PREFETCH ABORT"; break;
    case ROBCO_EXC_DABT:  what = "DATA ABORT"; break;
    case ROBCO_EXC_SVC:   what = "SYSTEM CALL IN THUMB STATE"; break;
    default:              what = "FAULT"; break;
    }
    return k_snprintf(buf, size, "ROM(1) %s AT 0x%08X", what, (unsigned)pc);
}
