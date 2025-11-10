#include "power.h"
#include <stddef.h>
#include <stdbool.h>

static power_mode_t current_mode = POWER_MODE_ACTIVE;

void power_init(void) {
    // Initialize power management
    current_mode = POWER_MODE_ACTIVE;
    
    // TODO: Setup battery monitoring
    // TODO: Configure power-saving features
}

uint32_t power_get_battery_voltage(void) {
    // TODO: Read actual battery voltage via ADC
    return 3700; // 3.7V placeholder (millivolts)
}

uint8_t power_get_battery_percentage(void) {
    // Simple voltage to percentage conversion
    // Assume 3.0V = 0%, 4.2V = 100% for LiPo
    uint32_t voltage = power_get_battery_voltage();
    
    if (voltage >= 4200) return 100;
    if (voltage <= 3000) return 0;
    
    return (uint8_t)((voltage - 3000) * 100 / 1200);
}

bool power_is_battery_low(void) {
    return power_get_battery_percentage() < 10;
}

void power_set_mode(power_mode_t mode) {
    current_mode = mode;
    
    switch (mode) {
        case POWER_MODE_ACTIVE:
            // Full speed
            break;
        case POWER_MODE_IDLE:
            // Reduce CPU speed
            break;
        case POWER_MODE_SLEEP:
            // Turn off display, wait for interrupt
            break;
        case POWER_MODE_DEEP_SLEEP:
            // Minimal power, RTC only
            break;
    }
}

power_mode_t power_get_mode(void) {
    return current_mode;
}

void power_enter_sleep(void) {
    power_set_mode(POWER_MODE_SLEEP);
    
    // Wait for interrupt (WFI)
    // ARMv6 (BCM2835) doesn't support WFI, use busy wait instead
#ifdef BCM2835
    // For ARMv6, just loop (WFI not supported)
    while (power_get_mode() == POWER_MODE_SLEEP) {
        // Busy wait until mode changes
    }
#else
    // For ARMv7+ use WFI instruction
    __asm__ volatile("wfi");
#endif
}
