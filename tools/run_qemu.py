#!/usr/bin/env python3
"""Boot Pip-OS in QEMU the way the firmware does, with the serial console here.

    tools/run_qemu.py <board> <image> [<sd.img>]

Uses the firmware stub and boot arguments of the RobCo-Core test harness.
Press Ctrl-A, then X, to quit QEMU.
"""

import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "external", "robco-core", "tests"))

from harness.boot import BOARDS, boot_args  # noqa: E402


def main(argv):
    if len(argv) not in (3, 4) or argv[1] not in BOARDS:
        print(f"usage: {argv[0]} <{'|'.join(BOARDS)}> <image> [<sd.img>]", file=sys.stderr)
        return 2
    board, image = argv[1], argv[2]
    sd = argv[3] if len(argv) == 4 else None
    cmd = [BOARDS[board].binary, "-M", board, "-nographic", "-no-user-config"]
    cmd += boot_args(image, board, sd=sd)
    os.execvp(cmd[0], cmd)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
