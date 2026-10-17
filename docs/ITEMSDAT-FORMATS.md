# items.dat format notes & version matrix

Growtopia's item database (`items.dat`) is a versioned binary file served to the
client on login (`PACKET_SEND_ITEM_DATABASE_DATA`). This documents the layout as
implemented in `include/database/items.cpp` and how it was verified.

## Header
- `u16` internal format version
- `u32` item count
- then `count` variable-length item entries (all strings are `u16` length-prefixed)

Item names are obfuscated: each byte XOR `PBG892FXX982ABC*[(i + item_id) % 16]`.

## Version-gated additions (per item, in parse order)
- `0x0b` punch-options string
- `0x0c` 4 bytes + 9 bytes
- `0x0d` 4 bytes (light range)
- `0x0e` 4 bytes
- `0x0f` 25 bytes + chair-texture string
- `0x10` item-renderer string
- `0x11` 4 bytes
- `0x12` 4 bytes (renderer hash)
- `0x13` 9 bytes
- `0x15` 2 bytes
- `0x16` info string
- `0x17` 2 x u16 (splice recipe)
- `0x18` 1 byte
- `0x19` hit-sound string + 4 bytes (see quirk)
- `0x1a` 1 byte
- `0x1b` trailing string (len-prefixed)  <- reverse-engineered 2026-10-17

### 0x19 quirk
The parser skips the 0x19 string payload only when its length > 6
(`if len > sizeof(short)+sizeof(int)`), a shortcut for the observed "default 0"
placeholder. Verified to parse real 0x19/0x1a/0x1b files exactly.

### 0x1b discovery (how it was done)
1. Community parser cross-check: [StileDevs/grow-items](https://github.com/StileDevs/grow-items)
   (`lib/Items.ts`) already handled `version >= 27` (= 0x1b): a single trailing
   length-prefixed string per item.
2. Diffed real files: `items-v5.57.dat` (0x1a) vs `items-v5.59.dat` (0x1b) from
   [StileDevs/itemsdat-archive](https://github.com/StileDevs/itemsdat-archive).
   The extra bytes per item match a len-prefixed string.
3. Root cause of the old boot crash: `if (version == 0x1a)` used equality, so
   0x1b files skipped the 0x1a byte and misaligned -> vector OOB assert. Fixed to `>=`.
4. Ground-truth validation: `tests/itemsdat_check.py` parses both files to an
   EXACT end-of-file with all 16k+ XOR-decoded names printable ASCII.

## Game version <-> format version matrix (observed)
| game | format | notes |
|---|---|---|
| 5.26-5.33 | 0x16 | |
| 5.36-5.37 | 0x17 | |
| 5.39-5.40 | 0x18 | |
| 5.46 | 0x19 | 16124 items |
| 5.47-5.57 | 0x1a | 16386 items (v5.57) |
| 5.59 | 0x1b | 16434 items |

Server support: **up to 0x1b**. Live file: v5.57 (0x1a) - matches the distributed
5.57 client. When players move to 5.58+/5.59+ clients, swap `items.dat` to
`items-v5.59.dat` (archive) and reboot: the server will log
`items.dat parsed successfully! (16434 items, format 0x1b)`.

## Validation tool
`python3 tests/itemsdat_check.py file1.dat [file2.dat ...]` - asserts exact EOF
consumption and printable names for every item. Run it whenever updating items.dat
before shipping.
