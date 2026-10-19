#!/usr/bin/env python3
"""
world_stream_check.py - parse a world-data stream the way the 5.57 client does and
assert it lands on EXACT EOF.

Why this exists: a missing/extra byte in ANY tile extra shifts every following tile,
and the only in-game symptom is silently missing or misplaced blocks - no error, no
log line. This is the regression guard for tile-extra payload widths.

The type byte after fg/bg/flags is only present for items whose *item type* maps to a
tile extra (see `get_type()` in include/database/world.cpp), so the checker needs
items.dat to know which tiles carry one.

usage:
  world_stream_check.py --world <file> [--items items.dat]
  world_stream_check.py --selftest
"""
import sys, struct, argparse

# item type (items.dat) -> extra-type byte, mirrors get_type() in world.cpp
ITEM_TYPE_TO_EXTRA = {
    0x01: 0x01,  # MAIN_DOOR
    0x02: 0x01,  # DOOR
    0x1a: 0x01,  # PORTAL
    0x07: 0x02,  # SIGN
    0x03: 0x03,  # LOCK
    0x13: 0x04,  # SEED
    0x24: 0x08,  # RANDOM
    0x26: 0x09,  # PROVIDER
    0x3d: 0x17,  # DISPLAY_BLOCK
    0x3e: 0x18,  # VENDING_MACHINE
}


class R:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u8(self):
        v = self.b[self.p]; self.p += 1; return v

    def u16(self):
        v = struct.unpack_from('<H', self.b, self.p)[0]; self.p += 2; return v

    def i16(self):
        v = struct.unpack_from('<h', self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]; self.p += 4; return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]; self.p += 4; return v

    def raw(self, n):
        v = self.b[self.p:self.p + n]; self.p += n; return v

    def str16(self):
        """std::string on the wire: u16 length + bytes (no NUL)"""
        return self.raw(self.u16())


def load_item_types(path):
    """id -> item type byte, via the items.dat format (mirrors tests/itemsdat_check.py)"""
    d = open(path, 'rb').read()
    version, = struct.unpack_from('<H', d, 0)
    count, = struct.unpack_from('<I', d, 2)
    pos = 6
    types = {}
    for _ in range(count):
        item_id, = struct.unpack_from('<H', d, pos); pos += 2
        pos += 2                                   # skip (mirrors itemsdat_check.py)
        pos += 1                                   # property
        pos += 1                                   # cat
        itype = d[pos]; pos += 1
        pos += 1                                   # skip
        types[item_id] = itype

        nlen, = struct.unpack_from('<h', d, pos); pos += 2 + nlen
        slen, = struct.unpack_from('<h', d, pos); pos += slen + 2
        pos += 4 + 1 + 4 + 4 + 1 + 1 + 4 + 1 + 2 + 1
        alen, = struct.unpack_from('<h', d, pos); pos += alen + 2
        pos += 4 + 4
        for _ in range(4):
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        pos += 16 + 4 + 2 + 2
        for _ in range(3):
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        pos += 80
        if version >= 0x0b:
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        if version >= 0x0c: pos += 13
        if version >= 0x0d: pos += 4
        if version >= 0x0e: pos += 4
        if version >= 0x0f:
            pos += 25
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        if version >= 0x10:
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        if version >= 0x11: pos += 4
        if version >= 0x12: pos += 4
        if version >= 0x13: pos += 9
        if version >= 0x15: pos += 2
        if version >= 0x16:
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
        if version >= 0x17: pos += 4
        if version >= 0x18: pos += 1
        if version >= 0x19:
            l, = struct.unpack_from('<h', d, pos); pos += 2
            if l > 6:
                pos += l
            pos += 4
        if version >= 0x1a: pos += 1
        if version >= 0x1b:
            l, = struct.unpack_from('<h', d, pos); pos += l + 2
    assert pos == len(d), f'items.dat parse drifted: {pos} != {len(d)}'
    return types


def read_extra(r, t, stats):
    """consume the payload for one tile-extra type, client-side field order"""
    if t == 0x01:      # door: label, dest, id, flags
        r.str16(); r.str16(); r.str16(); r.u8()
    elif t == 0x02:    # sign: label + u32
        r.str16(); r.u32()
    elif t == 0x03:    # lock: flags, owner, count, u32*
        r.u8(); r.i32()
        n = r.u32()
        for _ in range(n):
            r.u32()
    elif t == 0x04:    # seed: growTime u32 + fruit u8
        r.u32(); r.u8()
    elif t == 0x08:    # random/component: randValue u8
        stats['random'] += 1
        r.u8()
    elif t == 0x09:    # provider: growTime u32
        stats['provider'] += 1
        stats['grow'].append(r.u32())
    elif t == 0x17:    # display block: itemID i32
        stats['display'] += 1
        stats['shown'].append(r.i32())
    elif t == 0x18:    # vending: itemID, price, stock, worldLocks
        r.i32(); r.i32(); r.u32(); r.u32()
    else:
        raise ValueError(f'unhandled extra type {t:#04x} - extend this parser')


def parse(b, item_types, verbose=False):
    r = R(b)
    version = r.u16()
    flags = r.u32()
    name = r.str16().decode('latin-1')
    x = r.u32(); y = r.u32()
    total = r.u16()
    r.u32(); r.u16(); r.u8()          # 5.40+ prefix (7 bytes - what our server sends)

    stats = {'provider': 0, 'display': 0, 'random': 0, 'grow': [], 'shown': [], 'tiles': total}
    for i in range(total):
        fg = r.i16(); bg = r.i16()
        state = r.raw(4)
        if state[2] & 0x02:           # HAS_PARENT -> duplicate parent u16
            r.u16()
        if fg != 0:
            itype = item_types.get(fg)
            if itype is None:
                raise ValueError(f'tile {i}: fg={fg} not in items.dat')
            extra = ITEM_TYPE_TO_EXTRA.get(itype)
            if extra is None:
                continue              # no type byte on the wire for this item
            t = r.u8()
            if t != extra:
                raise ValueError(f'tile {i} ({i % x},{i // x}) fg={fg}: type byte {t:#04x} '
                                 f'but items.dat type {itype:#04x} implies {extra:#04x}')
            read_extra(r, t, stats)

    # @note our serializer writes 3 zero u32 after the tile block, matching GTopia's
    # `gameVersion >= 5.40` tail (WorldInfo::Serialize: uint32 unk x3). The client
    # reads them, so the checker must consume them too.
    r.u32(); r.u32(); r.u32()

    objs = r.i32(); r.i32()
    for _ in range(objs):
        r.u16(); r.u16(); r.u16(); r.u16(); r.u32()

    if verbose:
        print(f'  version={version:#x} flags={flags:#x} name={name!r} size={x}x{y} tiles={total}')
        print(f'  providers={stats["provider"]} displays={stats["display"]} objects={objs}')
        if stats['grow']:
            print(f'  provider growTime (secs): {stats["grow"]}')
        if stats['shown']:
            print(f'  displayed item ids: {stats["shown"]}')
    return r.p, len(b), version, flags, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--world', default=None)
    ap.add_argument('--items', default='items.dat')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args()

    if a.selftest:
        # header + one provider + one display tile
        body = struct.pack('<HI', 0x19, 0x40) + struct.pack('<H', 4) + b'TEST'
        body += struct.pack('<IIH', 100, 60, 2) + struct.pack('<IHB', 0, 0, 0)
        body += struct.pack('<hh', 1008, 0) + bytes(4) + bytes([0x09]) + struct.pack('<I', 1234)
        body += struct.pack('<hh', 2946, 0) + bytes(4) + bytes([0x17]) + struct.pack('<i', 9640)
        body += struct.pack('<III', 0, 0, 0)          # 5.40+ tail
        body += struct.pack('<ii', 0, 0)
        used, size, *_ = parse(body, {1008: 0x26, 2946: 0x3d}, verbose=True)
        assert used == size, f'selftest: parsed {used} of {size} bytes'
        print('selftest OK: stream lands on exact EOF')
        return 0

    if not a.world:
        ap.error('--world or --selftest required')

    item_types = load_item_types(a.items)
    b = open(a.world, 'rb').read()
    used, size, version, flags, stats = parse(b, item_types, verbose=True)
    print(f'parsed {used}/{size} bytes')
    if used != size:
        print(f'FAIL: {size - used} trailing bytes - a tile extra is the wrong width')
        return 1
    print('OK: exact EOF (every extra payload width is correct)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
