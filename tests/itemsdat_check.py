"""items.dat format validator.

Mirrors the C++ parser (include/database/items.cpp) byte-for-byte, including the
version>=0x19 quirk (skip the string payload only when len > 6), and supports up
to format 0x1b (trailing length-prefixed string per item).

Verification signals:
- the parse must consume the file EXACTLY (pos == file size)
- XOR-decoded item names must all be printable ASCII

Usage: python3 itemsdat_check.py file1.dat [file2.dat ...]
Exit code 0 when every file parses cleanly.
"""
import struct
import sys

TOKEN = b"PBG892FXX982ABC*"


def parse(path, verbose_names=5):
    d = open(path, "rb").read()
    size = len(d)
    pos = 0

    version, = struct.unpack_from("<H", d, pos); pos += 2
    count, = struct.unpack_from("<I", d, pos); pos += 4

    names = []
    for i in range(count):
        item_id, = struct.unpack_from("<H", d, pos); pos += 2
        pos += 2                       # skip
        pos += 1                       # property
        pos += 1                       # cat
        pos += 1                       # type
        pos += 1                       # skip

        nlen, = struct.unpack_from("<h", d, pos); pos += 2
        raw = bytearray(d[pos:pos + nlen]); pos += nlen
        for j in range(nlen):
            raw[j] ^= TOKEN[(j + item_id) % len(TOKEN)]
        names.append(raw.decode("latin1"))

        slen, = struct.unpack_from("<h", d, pos); pos += slen + 2   # texture
        pos += 4
        pos += 1
        pos += 4                       # ingredient (int)
        pos += 4
        pos += 1                       # collision
        pos += 1                       # hits
        pos += 4                       # hit_reset
        pos += 1                       # cloth_type / skip
        pos += 2                       # rarity
        pos += 1

        alen, = struct.unpack_from("<h", d, pos); pos += alen + 2   # audio dir
        pos += 4
        pos += 4                       # array<u8,4>
        for _ in range(4):
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        pos += 16
        pos += 4                       # tick
        pos += 2 + 2
        for _ in range(3):
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        pos += 80

        if version >= 0x0b:
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        if version >= 0x0c:
            pos += 4; pos += 9
        if version >= 0x0d:
            pos += 4
        if version >= 0x0e:
            pos += 4
        if version >= 0x0f:
            pos += 25
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        if version >= 0x10:
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        if version >= 0x11:
            pos += 4
        if version >= 0x12:
            pos += 4
        if version >= 0x13:
            pos += 9
        if version >= 0x15:
            pos += 2
        if version >= 0x16:
            l, = struct.unpack_from("<h", d, pos); pos += l + 2
        if version >= 0x17:
            pos += 2 + 2
        if version >= 0x18:
            pos += 1
        if version >= 0x19:
            l, = struct.unpack_from("<h", d, pos); pos += 2
            if l > 6:                  # quirk, mirrors the C++ comment {size} 0x00..
                pos += l
            pos += 4
        if version >= 0x1a:
            pos += 1
        if version >= 0x1b:
            l, = struct.unpack_from("<h", d, pos); pos += l + 2

    printable = sum(1 for n in names if n and all(0x20 <= ord(c) <= 0x7E for c in n))
    print(f"{path}: version=0x{version:02x} count={count} pos={pos} size={size} "
          f"exact={pos == size} printable_names={printable}/{count}")
    for n in names[:verbose_names]:
        print(f"   sample: {n!r}")
    return pos == size and printable == count


if __name__ == "__main__":
    ok = True
    for f in sys.argv[1:]:
        ok &= parse(f)
    sys.exit(0 if ok else 1)
