# Input calls

The rules are in the spec's [Input](https://github.com/RobCo-Industries/RobCo-Core/blob/891b3a25a1e38f88c214ef171f9c05c2f36bc6a1/docs/spec/PIPOS_ABI.md#input) section.

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
`time_ms` is the `ticks_ms` value when RobCo-Core received the event. The kernel converts RobCo-Core's stamp, taken on the system timer, to the `ticks_ms` clock. A stamp is never later than the `ticks_ms` value a ROM reads after it gets the event, and the stamps of successive events never go backwards. GPIO buttons and the dial are sampled from the tick interrupt, so their stamps show when the input happened. The console and the script channel are read only when the kernel polls, so their events carry the time of that poll.

## Behaviour

- The kernel polls for input in every input call and while `sleep_ms` waits.
- The image's queue holds 32 events. When it is full, a new event drops the oldest one.
- RobCo-Core has a queue of 32 events in front of the image's queue. It fills only while the kernel does not poll. Pip-OS sets it to drop its oldest event too, so if more than 32 events arrive between two polls, the ROM gets the newest 32 in order. Characters and kernel-only keys count toward the 32.
- The queue is emptied when an image starts and when it ends. Input that happens while no image runs never reaches an image.
- The kernel reads the serial ports only when it polls. At image start it reads all the bytes that wait in the UARTs and keeps their events. Bytes that reach a UART later belong to the image, even if the host sent them before the image started. The mini UART holds 8 bytes and the console UART 16; QEMU keeps the rest until the kernel reads.
- `read_buttons` comes from RobCo-Core's held-key state at the time of the call. It is the OR of the buttons each source holds. A release clears only the button its own source pressed. The state follows every button event, even one the queue dropped, so `read_buttons` is right after an overflow.
- `read_dial` comes from RobCo-Core's dial total, the sum of all detents since boot, at 15 degrees per detent. It starts at 0 at boot. It counts turns made while no image runs and turns whose events a full queue dropped. An image start does not reset it, so the dial never jumps between images.
- Every input call takes new input first, so a ROM that only polls still sees events.
- `wait_event` with timeout 0 does not block. Any other timeout is measured on the 1 MHz system timer, so it is never short. `PIP_WAIT_FOREVER` waits until an event comes.
- `wait_event` checks `timeout_ms`, then `out`, before it waits. On a timeout it writes nothing.
- `wait_event` returns `-PIP_ETIMEDOUT` only when no event is queued once the timeout has passed. An event that arrives just after the deadline, before the kernel checks, is returned. An event is never left in the queue behind a timeout result.
- The kernel runs its main-loop work while `wait_event` blocks, so script commands still answer.
- The kernel still prints an `ev=key` marker for every input event, also while an image runs.
- A button held down repeats: the first event with `value` 2 comes 500 ms after the press, then one every 100 ms until the release. Only the last button pressed repeats. Repeat events count toward the 32 like any other event.
