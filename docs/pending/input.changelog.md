### Added

- `read_buttons` and `read_dial` return the buttons held down and the dial position in degrees.
- `poll_event` and `wait_event` read button and dial events from a 32-event queue that drops the oldest event when full.
- `wait_event` waits with a timeout or forever, and the kernel keeps answering script commands meanwhile.
- Input that happens before a ROM starts never reaches it.
