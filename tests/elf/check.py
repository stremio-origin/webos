#!/usr/bin/env python3
"""Check ARM relocation layout against the webOS 3 glibc loader's assumptions."""
import struct
import sys
from pathlib import Path


def check(path):
    data = path.read_bytes()
    if data[:6] != b"\x7fELF\x01\x01" or struct.unpack_from("<H", data, 18)[0] != 40:
        raise ValueError("expected a 32-bit little-endian ARM ELF")
    phoff = struct.unpack_from("<I", data, 28)[0]
    entsize, count = struct.unpack_from("<HH", data, 42)
    segments = [
        struct.unpack_from("<8I", data, phoff + i * entsize)
        for i in range(count)
    ]
    dynamic = next(segment for segment in segments if segment[0] == 2)
    tags = dict(
        struct.unpack_from("<II", data, offset)
        for offset in range(dynamic[1], dynamic[1] + dynamic[4], 8)
    )
    if tags.get(18, 0) and tags.get(2, 0):
        if tags.get(20) != 17 or tags.get(19) != 8:
            raise ValueError("expected ARM REL relocation tables")
        if tags[17] + tags[18] != tags[23]:
            raise ValueError("DT_REL and DT_JMPREL must be adjacent on webOS 3")
    if not tags.get(30, 0) & 8:
        raise ValueError("BIND_NOW must remain enabled")


if __name__ == "__main__":
    paths = [Path(arg) for arg in sys.argv[1:]]
    if not paths:
        build = Path(__file__).resolve().parents[2] / "build" / "webos"
        paths = [build / "stremioorigin", *sorted(build.glob("libsb*.so"))]
    for path in paths:
        try:
            check(path)
        except (OSError, ValueError, struct.error, StopIteration, KeyError) as error:
            sys.exit(f"{path}: {error}")
        print(f"{path}: webOS 3 relocation layout OK")
