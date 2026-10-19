# Client binary RE — tile extras, provider growth, display/vending (GT 5.57 arm64)

Companion to `references/client-binary-re.md` (skill). This file is repo-side: the
**actionable** results of the 5.57 `libgrowtopia.so` audit that turned into code.

Tooling lives in `tools/gt-re/` (see its README): `variant_map.py`, `codexref.py`.

## 1. How variant names are really stored (important for any future hunt)

`On*` names exist in `.rodata` as plain cstrings — **but only the ones registered through
the string table**. Some names are assembled at runtime from inline immediates and appear
nowhere in `strings`:

```
0x134faa4: mov  w8, #0xe          ; length 14
0x134faa8: mov  w9, #0x6e4f       ; "On"  (0x6e4f little-endian = 'O','n')
0x134fab4: mov  w8, #0x6170       ; "pa"
0x134fab8: movk w9, #0x7053, lsl16 ; "Sp"
0x134fabc: movk w8, #0x6e77, lsl16 ; "wn"
```

That builds `OnSpawn` — the reason `OnSpawn`, `OnSetPos`, `OnSetBux`, `OnRemove`,
`OnKilled` are absent from a strings dump yet work on a real client. **Never conclude a
variant is unsupported because `strings` doesn't show it.**

232 `On*` names were enumerated from rodata; our server sends 35. The full delta is in
`docs/CLIENT-VARIANTS.md` (generated). Most of the rest are client-only UI events or
retail-only features (clash, guilds, auctions, dungeons).

## 2. Tile-extra type table (client jump table, ground truth)

`TileExtra::Serialize` @ `0x15b7558` dispatches on the extra-type byte via a **halfword
jump table** at VA `0x8010f8`, base `0x15b7764`, `ldrh` + `base + (entry << 2)`:

```
0x15b7748: adrp x9, #0x801000
0x15b774c: add  x9, x9, #0xf8
0x15b7750: adr  x10, #0x15b7764     ; switch base
0x15b7754: ldrh w11, [x9, x8, lsl #1]
0x15b7758: add  x10, x10, x11, lsl #2
0x15b7760: br   x10
```

Reading it as absolute 32-bit words (the obvious-but-wrong guess) produces garbage; the
`ldrh`+`lsl #2` form is what makes the table decodable. 82 entries, types `0x01..0x51`.

| extra | target | shape (fields after the type byte) |
|-------|--------|------------------------------------|
| 0x01 door | 0x15b7cbc | strings + flags |
| 0x02 sign | 0x15b7bbc | string + u32 |
| 0x03 lock | 0x15b7d74 | flags u8, owner i32, count u32, u32* |
| 0x04 seed | 0x15b7b58 | u32 growTime, u8 fruit |
| **0x09 provider** | 0x15b828c | **u32 growTime** (`add x0,x24,#0x5c` → `bl 0x15ba3a8` = u32 RW) |
| **0x17 display block** | 0x15b7d14 | **i32 itemID** (`add x0,x24,#0xc` → `bl 0x15ba340`) |
| **0x18 vending** | 0x15b7b80 | i32 itemID, i32 price, u32 stock, u32 worldLocks |
| 0x26 (38) | 0x15b7e68 | — (unused) |
| 0x40 | 0x15b7c04 | pointer/string |

The RW helpers confirm the widths exactly:

```
0x15ba310: 1-byte RW    0x15ba340: 4-byte RW
0x15ba3a8: 4-byte RW    0x15ba37c: resize a u32 vector
```

Cross-checked against GTopia `Source/World/TileExtra.h`:
`TILE_EXTRA_GROWTH(TileExtra_Provider, 9) { uint32 otherData; }`,
`TILE_EXTRA(TileExtra_DisplayBlock, 23) { int32 itemID; }`,
`TILE_EXTRA(TileExtra_VendingMachine, 24) { int32 itemID; int32 price; uint32 stock; uint32 worldLockCount; }`
— identical widths, independent derivation.

## 3. The bug this exposed in our server

`world::serialize()` emits the extra **type byte only** for provider / display block /
vending machine:

```cpp
if (char type = get_type(id_to_item(block.fg)); type > '\x00')
    blob.u8(type);          // ← and then nothing for 0x09 / 0x17 / 0x18
```

The client then reads a u32 that belongs to the *next tile*, and every tile after the
first provider in the world stream desyncs — silently, no error. Any world containing a
provider, display block or vending machine was corrupt on join.

## 4. Provider growth (`growTime`) semantics

`TileExtraGrowth` keeps `timer` (unix secs at placement) and `growTime` (seconds elapsed).
`FinalizeGrowth` recomputes `growTime = now - timer` on every serialize; the client's
harvest-ready test is `growTime >= item.tick`. So the wire field is **elapsed seconds**,
not a countdown, and `items.dat`'s `tick` is the harvest period in seconds:

| item | id | items.dat `tick` | wiki |
|------|----|------------------|------|
| ATM Machine | 1008 | 79200 | ~22 h ✅ |
| Chicken / Cow / Coffee Maker / Tea Set / Well | 872/866/1632/5116/2798 | 86400 | ~24 h ✅ |
| Science Station | 928 | 43200 | ~12 h ✅ |
| Sheep | 3888 | 259200 | ~3 days ✅ |
| Awkward Friendly Unicorn | 1636 | 604800 | ~7 days ✅ |
| Surgical Tool Bag / Star Tool Droid | 6212/6414 | 432000 | ~5 days ✅ |

Wiki (`growtopiawiki.com/w/Provider_(block_category)`) and `items.dat` agree 1:1, which
makes `tick` the authoritative period — no hardcoded table needed.

## 5. World-data header (`0x19` / `0x40` TODO)

The stock comments `@todo my rgt world says: 19 00` / `40 00 00 00` are correct retail
values, not guesses:

- `u16 worldVersion` = **0x19 (25)** — GTopia ships `DEFAULT_WORLD_VERSION_FOR_DB 27`
  (0x1b) for its own DB format; 0x19 is what 5.57-era retail writes.
- `u32 flags` = **0x40** — the "world has been visited / non-empty" style flag word.

Both are informational for a client (it does not gate parsing on them beyond the
`worldVersion > N` field additions in TileExtra), so writing the real values is safe and
makes captures comparable with retail.

## 6. Dialog commands confirmed present in 5.57

`add_progress_bar`, `add_textured_progress_bar`, `add_textured_progress_bar_with_icon`,
`add_clock`, `add_fish_info`, `add_dual_layer_icon_label`, `add_label_with_ele_icon`,
`add_player_info_small`, `add_smalltext_forced(_alpha)`, `add_text_input_password`,
`add_confirm_item_picker`, `add_embeddeed_url_button`, `add_community_button`,
`add_achieve(_button/_custom/_custom_btn)`, `add_iap_button`, `add_adwatch_button`.

Useful for provider/display/vending dialogs and any progress UI.
