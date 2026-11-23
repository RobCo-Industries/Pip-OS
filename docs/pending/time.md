# Time, sensor and power calls

The rules are in the [ABI spec](../../external/robco-core/docs/spec/PIPOS_ABI.md#time-sensors-and-power). This page lists what Pip-OS does on top of them.

| Number | Name | Arguments | Result | Errors |
|---|---|---|---|---|
| `0x30` | `read_sensor` | `uint32 sensor`, `uint32 *value` | 0 | `-PIP_EINVAL` for `sensor` above 6, `-PIP_EFAULT`, `-PIP_ENODEV` for sensors 1 to 6 and for sensor 0 while the clock is not set |
| `0x31` | `sensor_available` | `uint32 sensor` | 1 or 0 | `-PIP_EINVAL` for `sensor` above 6 |
| `0x40` | `get_time` | `uint32 *unix_seconds` | 0 | `-PIP_EFAULT`, `-PIP_ENODEV` while the clock is not set |
| `0x41` | `get_battery` | none | none | Always `-PIP_ENODEV` |
| `0x47` | `power` | `uint32 op` | 0 for 2 and 3; 0 and 1 do not return | `-PIP_EINVAL` for `op` above 3, `-PIP_EPERM` for a holotape |
| `0x4A` | `set_time` | `uint32 unix_seconds` | 0 | `-PIP_EPERM` for a holotape |

`-PIP_EPERM` is -1, `-PIP_EFAULT` is -14, `-PIP_EINVAL` is -22 and `-PIP_ENODEV` is -41.

## Clock

- Pip-OS keeps a software clock in Unix seconds. At boot it takes the time from the RTC chip when `ev=clock` says `rtc=present` and a time.
- `set_time` sets the software clock only. It never writes the chip, so the next boot starts again from the chip.
- With no chip and no `set_time`, the clock is not set. `get_time` and `read_sensor(0)` then return `-PIP_ENODEV` and write nothing.
- The clock counts on the 1 MHz system timer, so periods with interrupts off do not slow it. It only goes back through `set_time`.
- `set_time` accepts any value. The clock stops at `0xFFFFFFFF`, in 2106, instead of wrapping to 1970.
- `read_sensor` and `get_time` check the pointer first. A bad pointer returns `-PIP_EFAULT` even for a sensor that does not exist.

## Sensors

Sensor 0 is the clock. It gives the same value and result as `get_time`. Sensors 1 to 6 do not exist on the current hardware. `sensor_available(0)` returns 1 only while the clock is set.

## Power

| `op` | What happens |
|---|---|
| 0 | Prints `@RC stage=pipos ev=reboot`, then resets the board through the watchdog. The ROM loads again. |
| 1 | Prints `@RC stage=pipos ev=halt`, stops the sound, shows `PIP-OS HALTED. SAFE TO POWER OFF.` on the console, then stops with interrupts masked. Only a power cycle wakes it. |
| 2 | Turns the screen black. Drawing and `present` keep working on the back buffer. |
| 3 | Shows the last presented frame again. |
