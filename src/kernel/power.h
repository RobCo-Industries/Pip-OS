#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include <stdbool.h>

// Power modes
typedef enum {
    POWER_MODE_ACTIVE = 0,      // Full operation
    POWER_MODE_IDLE = 1,        // Reduced CPU speed
    POWER_MODE_SLEEP = 2,       // Display off, wake on button
    POWER_MODE_DEEP_SLEEP = 3   // RTC only
} power_mode_t;

// Initialize power management
void power_init(void);

// Battery monitoring
uint32_t power_get_battery_voltage(void);
uint8_t power_get_battery_percentage(void);
bool power_is_battery_low(void);

// Power mode control
void power_set_mode(power_mode_t mode);
power_mode_t power_get_mode(void);
void power_enter_sleep(void);

#endif // POWER_H
