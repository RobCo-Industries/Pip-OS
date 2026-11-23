/*
 * Running an image in user mode: the parts the ROM (rom.c) and a
 * holotape (sys_holo.c) share.
 */
#ifndef PIPOS_ROM_H
#define PIPOS_ROM_H

#include <stdint.h>

#include <robco/umode.h>

typedef struct pipos_image {
    int kind;                       /* PIP_KIND_ROM or PIP_KIND_HOLOTAPE */
    uint32_t entry;                 /* slot base */
    uint32_t sp;                    /* stack top */
    uint32_t startup;               /* the startup block, already written */
    uint32_t stub;                  /* the umode return stub */
    uint32_t load_size;             /* bytes loaded at entry, cleaned to memory */
    const umode_region_t *regions;
    unsigned region_count;
    const char *start_line;         /* marker printed right before the jump, without "@RC stage=pipos " */
} pipos_image_t;

/*
 * Cleans the loaded bytes, runs every group's image_start, prints the
 * start marker, runs the image until it ends, runs every image_end.
 * Returns umode_run()'s result: 0 and *ex filled, or a UMODE_ERR_*.
 */
int pipos_image_run(const pipos_image_t *im, umode_exit_t *ex);

/*
 * Prints the marker of a fault or a kill: "ev=<who>_kill reason=...".
 * screen: also the fault line on the screen console.
 */
void pipos_image_report_kill(const char *who, const umode_exit_t *ex, int screen);

#endif
