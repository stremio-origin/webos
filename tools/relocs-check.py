#!/usr/bin/env python3
# webOS 3's loader (glibc 2.19, BIND_NOW) reads DT_REL and DT_JMPREL as one table: they must be adjacent in every ARM
# binary we ship (tools/webos-relocs.ld keeps them so). tools/relocs-check.py <elf>...: exit 1 naming any that are not
import struct, sys
def adjacent(p):
    b = open(p, 'rb').read()
    if b[:4] != b'\x7fELF' or b[4] != 1: return True  # (not a 32-bit ELF: not ours to check)
    phoff, = struct.unpack_from('<I', b, 0x1c); phentsize, phnum = struct.unpack_from('<HH', b, 0x2a); tags = {}
    for i in range(phnum):
        t, off, va, pa, fs, ms, fl, al = struct.unpack_from('<IIIIIIII', b, phoff + i * phentsize)
        if t == 2:
            for k in range(0, fs, 8):
                d, v = struct.unpack_from('<iI', b, off + k)
                if d == 0: break
                tags[d] = v
    rel, relsz, jmprel = tags.get(17), tags.get(18, 0), tags.get(23)
    return rel is None or jmprel is None or rel + relsz == jmprel
bad = [p for p in sys.argv[1:] if not adjacent(p)]
for p in bad: print('relocs-check: %s: DT_REL and DT_JMPREL are not adjacent (webOS 3 will not start it): link with tools/webos-relocs.ld' % p, file=sys.stderr)
sys.exit(1 if bad else 0)
