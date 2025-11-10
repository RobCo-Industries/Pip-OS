#ifndef BOOT_DISPLAY_H
#define BOOT_DISPLAY_H

#include <stdint.h>

// Matrix-style boot display functions
void matrix_display_init(void);
void matrix_display_run(uint32_t duration_ms);
void boot_messages_display(void);

#endif // BOOT_DISPLAY_H
