"""Input events from the script channel and the serial console become ev=key markers."""

from pipos_helpers import TIMEOUT, booted, no_exception, one, wait_markers


def keys(vm, src):
    return [(m["type"], m["code"], m["value"])
            for m in wait_markers(vm, "key", count=0) if m["src"] == src]


def test_script_channel(board, kernel_image):
    with booted(kernel_image, board, serial2=True) as vm:
        vm.wait_for("ready\n", timeout=TIMEOUT, port=1)
        for line in ("key up", "key select", "dial -2", "char x"):
            vm.send(line + "\n", port=1)
        wait_markers(vm, "key", count=6, src="script")
        assert keys(vm, "script") == [
            ("key", "up", "1"), ("key", "up", "0"),
            ("key", "select", "1"), ("key", "select", "0"),
            ("dial", "0", "-2"),
            ("char", str(ord("x")), "0"),
        ], vm.serial
        replies = [line for line in vm.lines(port=1) if line.startswith(("ok", "err"))]
        assert replies == ["ok"] * 4, vm.serial2
        no_exception(vm)


def test_serial_console_keys(board, kernel_image):
    with booted(kernel_image, board) as vm:
        vm.send("\x1b[B")
        wait_markers(vm, "key", count=2, src="serial")
        assert keys(vm, "serial")[:2] == [("key", "down", "1"), ("key", "down", "0")]
        assert one(vm, "ready")["rom"] == "0"
