#!/usr/bin/env python3
"""Writes a data holotape (subtype 2) from a sealed holotape.

usage: mkdata.py IN OUT

Pip-OS must refuse to launch it. `rimg.py seal` refuses to seal it, since
a data holotape has no load slot, so this script sets the subtype and
writes the CRC itself with rimg.image_crc.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                "..", "external", "robco-core", "tools"))
import rimg  # noqa: E402

SUBTYPE_OFFSET = rimg.HDR_OFFSET + 0x28
DATA_SUBTYPE = 2


def main():
    src, dst = sys.argv[1:3]
    with open(src, "rb") as f:
        data = bytearray(f.read())
    struct.pack_into("<I", data, SUBTYPE_OFFSET, DATA_SUBTYPE)
    struct.pack_into("<I", data, rimg.CRC_OFFSET, rimg.image_crc(data, len(data)))
    with open(dst + ".tmp", "wb") as f:
        f.write(data)
    os.replace(dst + ".tmp", dst)


if __name__ == "__main__":
    main()
