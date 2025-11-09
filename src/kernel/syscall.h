#ifndef SYSCALL_H
#define SYSCALL_H

#include <stdint.h>
#include <stdbool.h>

// System Call Numbers (from development plan)
#define SYSCALL_DRAW_LINE           0x01
#define SYSCALL_DRAW_POINT          0x02
#define SYSCALL_DRAW_TEXT           0x03
#define SYSCALL_CLEAR_SCREEN        0x04
#define SYSCALL_READ_BUTTONS        0x10
#define SYSCALL_READ_DIAL           0x11
#define SYSCALL_PLAY_TONE           0x20
#define SYSCALL_PLAY_SAMPLE         0x21
#define SYSCALL_READ_SENSOR         0x30
#define SYSCALL_GET_TIME            0x40
#define SYSCALL_GET_BATTERY         0x41
#define SYSCALL_READ_SAVE           0x50
#define SYSCALL_WRITE_SAVE          0x51

// Button definitions
typedef enum {
    BUTTON_UP = 0,
    BUTTON_DOWN = 1,
    BUTTON_LEFT = 2,
    BUTTON_RIGHT = 3,
    BUTTON_SELECT = 4,
    BUTTON_DIAL_PRESS = 5
} button_t;

// Sensor types
typedef enum {
    SENSOR_RTC = 0,
    SENSOR_BATTERY = 1,
    SENSOR_GEIGER = 2,
    SENSOR_GPS = 3,
    SENSOR_ACCEL = 4,
    SENSOR_TEMP = 5,
    SENSOR_HEARTRATE = 6
} sensor_type_t;

// System call handler
typedef int32_t (*syscall_handler_t)(uint32_t arg0, uint32_t arg1, 
                                     uint32_t arg2, uint32_t arg3);

// Initialize system call interface
void syscall_init(void);

// System call implementations

// Display operations
int32_t sys_draw_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
int32_t sys_draw_point(int32_t x, int32_t y, uint32_t color);
int32_t sys_draw_text(int32_t x, int32_t y, const char* text, uint32_t size);
int32_t sys_clear_screen(uint32_t color);

// Input operations
uint32_t sys_read_buttons(void);
int32_t sys_read_dial_position(void);
int32_t sys_read_dial_delta(void);

// Audio operations
int32_t sys_play_tone(uint32_t frequency, uint32_t duration);
int32_t sys_play_sample(const uint8_t* data, uint32_t length);

// Sensor operations
int32_t sys_read_sensor(sensor_type_t sensor, uint32_t* value);
bool sys_sensor_available(sensor_type_t sensor);

// System operations
int32_t sys_get_time(uint32_t* time_ptr);
uint32_t sys_get_battery_level(void);

// Storage operations
int32_t sys_read_save(uint32_t offset, void* buffer, uint32_t size);
int32_t sys_write_save(uint32_t offset, const void* buffer, uint32_t size);

#endif // SYSCALL_H
