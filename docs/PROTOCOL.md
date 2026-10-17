# Protocol cheat-sheet (this fork's server)

Condensed from the source; paths are relative to the repo root. Purpose: a fast map of
the wire protocol when adding features (trade, redeem codes, clash, gacha, ...) or
writing packet tooling. Complements `docs/ITEMSDAT-FORMATS.md` (items.dat file format)
and `docs/DEV-NOTES.md` (operational patching).

## 1. Ports & transports

- **HTTPS discovery server** - TCP **443** (`include/https/https.cpp`,
  `GUROTOPIA_HTTPS_PORT` env override). Single-threaded accept loop, one request per
  connection, response then close.
- **ENet game server** - UDP **`server_data.port`** (default 17091) (`main.cpp`).
  Host config: 50 max peers, 2 channels, `usingNewPacketForServer = true`,
  `enet_crc32` checksum, range-coder compression. All game sends use channel 0,
  `ENET_PACKET_FLAG_RELIABLE`.

## 2. Discovery: HTTPS `server_data`

- Client POSTs `/growtopia/server_data.php` (body carries `ltoken|...`; **size varies
  by client version** - this fork answers any request up to 4096 bytes; upstream replied
  only to exactly-213-byte requests and silently dropped newer clients).
- Reply (HTTP/1.1 200, `text/plain`, `Connection: close`), body:

  ```
  server|<ip>\n
  port|<n>\n
  type|<int>\n
  type2|<int>\n
  #maint|<str>\n
  loginurl|<url>\n
  meta|<str>\n
  RTENDMARKERBS1001
  ```

- Config source of truth: `./server_data.php` (line pairs `key|value`, parsed in
  `include/https/server_data.cpp::init()`; response built in `https::listener()`).
- TLS: clients >=5.4x validate certs - serve the FULL chain
  (`SSL_CTX_use_certificate_chain_file`). Self-signed fails with `SSL alert number 46`
  and shows as a generic "check your internet" error in the client.

## 3. ENet message framing (`include/eventType/receive.cpp`)

First byte of every packet selects the class:

- **`2` / `3` - text message.** Payload slice = bytes `[4 .. dataLength-2]` (4-byte
  header, last byte stripped). Newlines are replaced with `|`, then split into pipes.
- **`4` - tank (game) packet.** Requires `dataLength >= 60` (sizeof gamePacket).
- anything else: logged (`received unique: N`) and dropped.

Text **action routing**:

| first pipes                    | handler key              |
|--------------------------------|--------------------------|
| `protocol` / `tankIDName`      | pipes[0] itself          |
| `requestedName` (modern login) | routed to `tankIDName`   |
| everything else                | `"pipes[0]|pipes[1]"` e.g. `action|enter_game` |

Guards: needs >=2 pipes; **punch coords are validated to [0,100)x[0,60)** before
dispatch (garbage punch values used to crash the server indexing `blocks[]`).

## 4. Tank packet wire layout (60 bytes, little-endian)

| off | type  | field          | notes                                            |
|-----|-------|----------------|--------------------------------------------------|
| 0   | int   | packet_create  | always 4 (`NET_MESSAGE_GAME_PACKET`)             |
| 4   | int   | type           | see section 5                                    |
| 8   | int   | netid          | per-world peer identity (assigned on world join; `-1` in varlists = unset) |
| 12  | int   | uid            | account user id                                  |
| 16  | int   | state          | bitfield (below)                                 |
| 20  | float | count          | purpose unknown                                  |
| 24  | int   | id             | active hand: 18 = fist/punch, 32 = wrench        |
| 28  | float | pos.x          | pixels                                           |
| 32  | float | pos.y          | pixels                                           |
| 36  | float | speed.x        | velocity                                         |
| 40  | float | speed.y        | gravity (higher = smaller jumps)                 |
| 44  | float | idk            | unknown                                          |
| 48  | int   | punch.x        | **tile coords** (int32)                          |
| 52  | int   | punch.y        | tile coords                                      |
| 56  | u32   | size           | payload length following the header              |

`state` flags (`enum state`, `include/database/peer.hpp`): `S_UPDATE 0x4`,
`S_EXTENDED 0x8`, `S_MOVE_LEFT 0x10`, `S_MOVE_RIGHT 0x20`, `S_LAVA_HIT 0x40`,
`S_JUMP 0x80`, `S_ACTIVATE_OBJECT 0x4000`.

Parse: `make_gamePacket()` (`include/database/peer.cpp`) - raw i32/f32 reinterpret.
Serialize: `compress_state()` - writes the 15 fields; callers append payloads
(`::blob` writer in `include/database/database.hpp`, also used for map/inventory blobs).

## 5. Tank types in use

**Client -> server** (`gamePacket_pool`, `include/gamePacket/_gamePacket.cpp`):

| type | handler              |
|------|----------------------|
| 0x00 | movement             |
| 0x03 | tile_change          |
| 0x07 | tile_activate        |
| 0x0a | item_activate        |
| 0x0b | item_activate_object |
| 0x15 | ping_reply           |
| 0x1a | disconnect           |

**Server -> client** (as used in this source):

| type | meaning                       | where                                            |
|------|-------------------------------|--------------------------------------------------|
| 0x01 | PACKET_CALL_FUNCTION (RPC)    | `send_varlist()` - section 6                     |
| 0x04 | PACKET_SEND_MAP_DATA          | `join_request.cpp` + `world::serialize()` payload |
| 0x09 | PACKET_SEND_INVENTORY_STATE   | `peer.cpp` (`u8 0x01` + serialized inventory)    |
| 0x0e | PACKET_ITEM_CHANGE_OBJECT     | `world.cpp`                                      |
| 0x0f | PACKET_SEND_LOCK              | `tile_change.cpp` (lock visuals)                 |
| 0x10 | PACKET_SEND_ITEM_DATABASE     | `items.cpp` (whole items.dat blob)               |
| 0x11 | PACKET_SEND_PARTICLE_EFFECT   | `world.cpp`                                      |
| 0x14 | PACKET_SET_CHARACTER_STATE    | `SetClothing.cpp` (`type = 0x14 | ((0x808000 + effect) << 8)`) |
| 0x16 | PACKET_PING_REQUEST           | `enter_game.cpp`                                 |

## 6. Variant RPC - the Proton layer (`include/proton/`)

Server -> client calls are tank `type 0x01`, `state = S_EXTENDED`,
`netid` = target (or -1), `id` = delay, `size` = serialized length; the serialized
VariantList follows immediately after the 60-byte header (`send_varlist()`).

VariantList serialization (`SerializeToMem`): `[u8 count]` then per used slot:
`[u8 index][u8 type][data]`. Data sizes: float/int32/uint32 = 4, vector2 = 8,
vector3 = 12, string = `u32 length` + bytes. Max 8 slots
(`C_MAX_VARIANT_LIST_PARMS`).

Variant types (`Variant::eType`): 0 UNUSED, 1 FLOAT, 2 STRING, 3 VECTOR2, 4 VECTOR3,
5 UINT32, 6 ENTITY, 7 COMPONENT, 8 RECT, 9 INT32. C++ overloads pick the type
automatically (`"str"` -> STRING, `u_int` -> UINT32, `int` -> INT32, `float`, `(x,y)`,
`(x,y,z)`).

Builders in `include/onVariant/`: Action, BillboardChange, ConsoleMessage, CountryState,
EmoticonDataChanged, NameChanged, RequestGazette, RequestWorldSelectMenu, SetBux,
SetClothing, Spawn.

## 7. Login flow end-to-end (modern client, observed on 5.57)

1. **HTTPS discovery** (section 2). Client UI: "Getting server address...".
2. **ENet conn #1** - text `protocol|226\nltoken|<b64>`: `pipes[2]=="ltoken"`,
   `pipes[3]` = base64 of a token blob containing `growId=..&password=..`. Server runs
   `peer_auth()` (salted sha256, legacy plaintext auto-upgrade), `peer::load()`, ban
   check. Success -> varlist **`OnSendToServer`**(port, 0, uid, `"<server>|0|0"`, 1,
   growid). Failure -> send_action **`logon_fail`** (`include/action/protocol.cpp`).
3. Client acks (tank `0x1a`), disconnects and **reconnects**.
4. **ENet conn #2** - modern session payload starting **`requestedName|`** carrying
   `user|<uid>` (plus `protocol|226`, `country|..`, ~40 pipes). Routed to the
   `tankIDName` handler: growid resolved via `SELECT growid FROM peer WHERE uid=?`,
   peer loaded, then two varlists (`include/action/tankIDName.cpp`):
   - **`OnOverrideGDPRFromServer`** (18, 1, 0, 1)
   - **`OnSuperMainStartAcceptLogonHrdxs47254722215a`**(items-hash 1086086596,
     `"ubistatic-a.akamaihd.net"`, `"0098/024920264/cache/"`, anti-cheat app list,
     `key=value` options string + `eventButtons` JSON)
5. **`action|refresh_item_data`** -> ConsoleMessage + one big tank `0x10` with the
   whole items.dat blob (`include/action/refresh_item_data.cpp`, `items.cpp`).
6. **`action|enter_game`** -> welcome console messages, inventory state (tank `0x09`),
   SetBux, SetHasGrowID, OnTodaysDate, RequestWorldSelectMenu, RequestGazette,
   tank `0x16` ping request, OnSetFeatureEnableFlags (`enter_game.cpp`).
7. **`action|join_request|name|WORLD`** -> world load/create, entry gate (owner /
   public / access list / role), then tank `0x04` map blob + spawn sequence
   (`join_request.cpp`).

Pitfalls learned: dispatching text only on `protocol`/`tankIDName` silently drops
stage 4 (client hangs at "Logging on..."); bound-check pipe loops; `user|` parsed with
`std::stoi` can throw on garbage.

## 8. Text protocol details

- Server -> client: `send_action(peer, action, str)` = 4-byte header (byte0 = 3) +
  `action|<name>\n` + str, zero-padded (`include/database/world.cpp`).
- Client text payloads follow `key|value|key|value` conventions; parsed pairwise by
  `::hPipe` (`include/tools/string.hpp`) or `readch(str, '|')`.
- **dialog_return routing**: `action|dialog_return|dialog_name|<id>|...` ->
  `dialog_return_pool` (`include/action/dialog_return/__dialog_return.cpp`). Handlers:
  popup, drop_item, trash_item, find_item, gateway_edit / door_edit / sign_edit,
  billboard_edit, lock_edit, create_blast, socialportal, megaphone. Dialogs are built
  with `include/tools/create_dialog.hpp`.
- **In-game commands**: chat starting with `/` -> `cmd_pool`
  (`include/commands/__command.cpp`): help/?, time, find, warp, punch, skin, sb, who,
  me, news, weather, ghost, give, setrole, kick, ban, unban, mute, setgems + 24 emote
  aliases.

## 9. Extending (recipes)

1. **New text action** (e.g. `action|trade_accept`): `include/action/X.cpp`
   (`void action::X(ENetEvent&, const std::string&)`) + register in
   `include/action/__action.cpp` action_pool.
2. **New game packet type**: `include/gamePacket/X.cpp` + register in
   `_gamePacket.cpp` gamePacket_pool.
3. **New outbound RPC**: `include/onVariant/X.cpp` calling
   `send_varlist(peer, {"RpcName", arg0, arg1, ...})`.
4. **New dialog result**: `include/action/dialog_return/X.cpp` + register in
   `__dialog_return.cpp`; build the dialog with create_dialog helpers.
5. **New /command**: `include/commands/X.cpp` + cmd_pool + `cmd_requires_arg`
   (fixed-size `std::array` - bump the size in BOTH `__command.hpp` and
   `__command.cpp` or the build dies) + `named_help` text.
6. **Persistence**: worlds save through `world::save()` only; peer columns need a boot
   migration in `create_table_if_not_exist()` + `peer::mysql_select_all()` (see
   `docs/DEV-NOTES.md`).

## 10. Diagnostics & testing

- `./main.out --verbose` logs text payloads (1500 chars), tank types, HTTPS request
  size/IP/first line, varlist sends. Quiet by default; auth failures always logged.
- Live logs: `tmux capture-pane -t gtps:server -p` (pty = line-buffered).
- Simulators in `tests/` (see `tests/README.md`): `https_login_test.py`,
  `sim_protocol_login.js`, `sim_modern_login.js`, `multi_client_smoke.js`,
  `sim_lock_flow.js`, `sim_punch.js`, `sim_command.js`, `itemsdat_check.py`.
- items.dat internals (version gates, XOR name token `PBG892FXX982ABC*`, per-version
  table): `docs/ITEMSDAT-FORMATS.md`.

## 11. Community references

- github.com/H-pun/growtopia-docs - protocol docs (items.dat, HTTP + ENet, packets)
- github.com/NetroIndonesia/Growtopia - 11-part GTPS protocol handbook
- github.com/GrowtopiaNoobs/GrowDocs - older packet documentation
