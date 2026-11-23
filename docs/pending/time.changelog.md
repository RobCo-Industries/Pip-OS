### Added

- The time and sensor calls `read_sensor`, `sensor_available`, `get_time`, `get_battery` and `set_time`. The clock starts from the RTC chip when one answers.
- `set_time` sets the software clock only and never writes the chip.
- `power`: reboot through the watchdog, halt, screen off and screen on.
- The `ev=reboot` and `ev=halt` markers before a reboot or a halt.
