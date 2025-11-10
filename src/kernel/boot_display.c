#include "boot_display.h"
#include "uart.h"
#include "k_libc/k_stdio.h"
#include <stddef.h>

// Character set for matrix display
static const char matrix_chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@#$%&*()[]{}+-*/=<>♠♣♥♦◊○●□■";

// Simple random number generator for matrix effect
static uint32_t random_seed = 12345;

static uint32_t simple_rand(void) {
    random_seed = random_seed * 1103515245 + 12345;
    return (random_seed / 65536) % 32768;
}

void matrix_display_init(void) {
    // Initialize random seed with some variation
    random_seed = 12345;
}

void matrix_display_run(uint32_t duration_ms) {
    const uint32_t cols = 80;
    const uint32_t rows = 24;
    const uint32_t iterations = duration_ms / 100; // Update every 100ms
    
    for (uint32_t iter = 0; iter < iterations; iter++) {
        // Clear screen (simple approach)
        if (iter == 0) {
            k_printf("\033[2J\033[H"); // ANSI clear screen and home
        }
        
        // Generate random matrix characters
        for (uint32_t i = 0; i < 20; i++) {
            uint32_t col = simple_rand() % cols;
            uint32_t row = simple_rand() % rows;
            uint32_t char_idx = simple_rand() % (sizeof(matrix_chars) - 1);
            
            // Print character at position (simplified)
            k_printf("%c", matrix_chars[char_idx]);
        }
        
        // Delay
        for (volatile uint32_t d = 0; d < 500000; d++);
    }
}

void boot_messages_display(void) {
    // Clear any previous output
    k_printf("\r\n\r\n");
    
    // Display boot sequence messages
    k_printf("LOADER V1.1\r\n");
    
    // Small delay
    for (volatile uint32_t d = 0; d < 1000000; d++);
    
    k_printf("EXEC VERSION 41.10\r\n");
    
    for (volatile uint32_t d = 0; d < 500000; d++);
    
    k_printf("64K RAM SYSTEM\r\n");
    k_printf("38911 BYTES FREE\r\n");
    
    for (volatile uint32_t d = 0; d < 500000; d++);
    
    k_printf("NO HOLOTAPE FOUND\r\n");
    
    for (volatile uint32_t d = 0; d < 500000; d++);
    
    k_printf("LOAD ROM(1): DEITRIX 303\r\n");
    
    k_printf("\r\n");
}
