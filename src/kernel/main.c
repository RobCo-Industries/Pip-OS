#include "uart.h"

#include <stddef.h>
#include <stdint.h>

#include "framebuffer.h"
#include "mailbox.h"
#include "k_libc/k_stdio.h"
#include "video.h"
#include "boot_display.h"
#include "rom_loader.h"
#include "syscall.h"
#include "power.h"
#include "audio.h"

void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
    (void) r0;
    (void) r1;
    (void) atags;

    // Initialize UART for debug output
    uart_init();

    // Display header
    k_printf("\r\n");
    k_printf("*************** PIP-OS(R) V7.1.0.8 ***************\r\n");
    k_printf("COPYRIGHT 2075 ROBCO IND.\r\n");
    k_printf("**************************************************\r\n");
    k_printf("\r\n");

    // Initialize subsystems
    k_printf("Initializing subsystems...\r\n");
    
    // Initialize power management
    power_init();
    k_printf("  [OK] Power management\r\n");
    
    // Initialize audio system
    audio_init();
    k_printf("  [OK] Audio system\r\n");
    
    // Initialize system call interface
    syscall_init();
    k_printf("  [OK] System call interface\r\n");

    // Display boot sequence
    k_printf("\r\n");
    k_printf("Starting boot sequence...\r\n");
    k_printf("\r\n");

    // Run matrix display animation (simplified for UART)
    matrix_display_init();
    k_printf("Matrix display initialization...\r\n");
    // matrix_display_run(3000); // 3 seconds - commented out for faster boot
    
    // Play boot audio sequence (stub for now)
    // audio_boot_sequence(); // Commented out until PWM audio is implemented
    
    // Display boot messages
    boot_messages_display();
    
    k_printf("\r\n");
    k_printf("**************************************************\r\n");
    k_printf("PIP-OS KERNEL INITIALIZED\r\n");
    k_printf("**************************************************\r\n");
    k_printf("\r\n");

    // Check for holotape
    k_printf("Checking for holotape...\r\n");
    if (holotape_detect()) {
        k_printf("  Holotape detected!\r\n");
        if (holotape_load()) {
            k_printf("  Holotape loaded successfully\r\n");
            // Execution would transfer to holotape
        }
    } else {
        k_printf("  No holotape found\r\n");
    }
    
    k_printf("\r\n");

    // Check for ROM
    k_printf("Checking for ROM...\r\n");
    if (rom_detect()) {
        k_printf("  ROM detected!\r\n");
        if (rom_load()) {
            k_printf("  ROM loaded successfully\r\n");
            // Execution would transfer to ROM
        }
    } else {
        k_printf("  No ROM found\r\n");
    }

    k_printf("\r\n");
    k_printf("**************************************************\r\n");
    k_printf("PIP-OS READY\r\n");
    k_printf("Battery: %d%%\r\n", power_get_battery_percentage());
    k_printf("**************************************************\r\n");
    k_printf("\r\n");

    // System information
    k_printf("Hardware Information:\r\n");
    k_printf("  Core clock : %d Hz\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_CORE));
    k_printf("  ARM  clock : %d Hz\r\n", mailbox_get_id(MAILBOX_TAG_GET_CLOCK_RATE, MAIL_CLOCK_ARM));
    k_printf("  Board model: %d\r\n", mailbox_get(MAILBOX_TAG_GET_BOARD_MODEL));
    k_printf("  Board rev  : %d\r\n", mailbox_get(MAILBOX_TAG_GET_BOARD_REVISION));
    k_printf("\r\n");

    // Main loop - echo UART input
    k_printf("Entering main loop (UART echo mode)...\r\n");
    k_printf("Type characters to echo them back.\r\n");
    k_printf("\r\n");
    
    while (1) {
        uart_putc(uart_getc());
    }
}