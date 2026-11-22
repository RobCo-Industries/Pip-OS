# Input calls

The rules are in the spec's [Input](https://github.com/RobCo-Industries/RobCo-Core/blob/1e0fb655ea09497d133c6e3c3b086a2bb8cbca0e/docs/spec/PIPOS_ABI.md#input) section.

| Number | Name | Arguments | Result |
|---|---|---|---|
| `0x10` | `read_buttons` | none | The `PIP_BTN_*_BIT` mask of the buttons down now |
| `0x11` | `read_dial` | none | The dial position in degrees, 0 to 359 |
| `0x12` | `poll_event` | `pip_event_t *out` | 1 with the oldest event in `*out`, or 0 when the queue is empty. A bad `out` returns `-PIP_EFAULT`. |
| `0x13` | `wait_event` | `pip_event_t *out`, `uint32 timeout_ms` | 1 with the oldest event in `*out`, or `-PIP_ETIMEDOUT` (-42) when `timeout_ms` passed first. `-PIP_ERANGE` for a timeout above `0x7FFFFFFF` other than `PIP_WAIT_FOREVER`. A bad `out` returns `-PIP_EFAULT`. |

## Events

| Input | Event |
|---|---|
| Up, down, left, right, select, dial push | `PIP_EV_BUTTON` with `code` 0 to 5, the bit of `read_buttons`. `value` is 1 press, 0 release, 2 repeat. |
| One dial turn | `PIP_EV_DIAL` with `code` 0 and the signed detent count |
| Back, F1 to F4, characters | Nothing. Only the kernel sees them. |

Events come from the GPIO buttons and dial, the serial console and the script channel alike.
`time_ms` is the `ticks_ms` value when the kernel took the event.

## Behaviour

- The queue holds 32 events. When it is full, a new event drops the oldest one.
- The queue is emptied when an image starts and when it ends. Input that happens while no image runs never reaches an image.
- `read_buttons` is the OR of the buttons each source holds. A release clears only the button its own source pressed.
- `read_dial` starts at 0 at boot and moves 15 degrees per detent. It counts turns made while no image runs too.
- Every input call takes new input first, so a ROM that only polls still sees events.
- `wait_event` with timeout 0 does not block. Any other timeout is measured on the 1 MHz system timer, so it is never short. `PIP_WAIT_FOREVER` waits until an event comes.
- `wait_event` checks `timeout_ms`, then `out`, before it waits. On a timeout it writes nothing.
- The kernel runs its main-loop work while `wait_event` blocks, so script commands still answer.
- The kernel still prints an `ev=key` marker for every input event, also while an image runs.
- Pip-OS does not turn on key repeat yet, so a ROM gets no repeat events today.
