# gtproxy - capture proxy for GTPS protocol work

Transparent ENet relay that sits between a Growtopia client and the game server,
logs every application message, and passes everything through unchanged. Built for
the reverse-engineering track: capturing what the *retail client* sends for features
the server does not implement yet (trade, redeem codes, clash, gacha, ...).

Protocol reference: `docs/PROTOCOL.md`. This tool is not built into the server and
is never required for normal operation.

## What it does

- One relay per client connection: `client <-> proxy <-> server`, at the ENet
  *message* level (handshakes, reliability and compression are handled per leg).
- Every message is decoded for the console and the jsonl log: text (`protocol|..`,
  `action|..`), tank packets (fields + variant-RPC varlists), anything else as raw.
- Byte-for-byte relay (verified: direct vs proxied sessions receive identical
  packets).

## ENet interop notes (learned while building this)

- The game server only answers peers in **new-packet mode**: the connecting host
  must set `usingNewPacket = 1` (plus `host->checksum = enet_crc32` and the
  range-coder). Anything else is dropped *silently* - no connect, no error.
  (Verified with a flag-matrix probe; the proxy sets all three on its
  server-facing host.)
- The client-facing host mirrors the game server exactly
  (`usingNewPacketForServer = 1`, crc32, range coder). `--no-new-packet` disables
  the flag for debugging.
- On connect the server sends a 5-byte beacon (`01 00 00 00 xx`): a little-endian
  int status (1 = OK, 0 = overloaded -> logon_fail) plus 1 *uninitialized* byte -
  `connect.cpp` allocates 5 bytes but only writes 4 (the last byte is garbage from
  the allocator). Harmless.

## Build

    cd tools/gtproxy
    PATH=$HOME/opt/miniforge3/envs/gtps/bin:$PATH make

Links the vendored `include/enet` static lib. The `gtproxy` binary is gitignored.

## Run A - test mode (server untouched)

Server keeps its normal ports; the proxy listens elsewhere and forwards:

    tools/gtproxy/gtproxy --listen 17191 --backend 127.0.0.1:17091

    # the simulators honor HOST/PORT env overrides:
    cd tests
    NODE_PATH=<dir with growtopia.wasm> PORT=17191 node sim_command.js

## Run B - capture mode (live traffic, e.g. from a phone)

Move the server's ENet bind to an alternate port while `server_data.php` keeps
advertising the normal one (17091), then put the proxy on the public port:

    # 1) server binds 17092; HTTPS stays direct on 443; config unchanged
    GUROTOPIA_ENET_PORT=17092 ~/opt/gtps-restart.sh

    # 2) proxy owns the advertised game port (run outside the gtps session)
    tmux new-session -d -s gtproxy \
      "cd \$HOME/projects/Gurotopia && tools/gtproxy/gtproxy \
         --listen 17091 --backend 127.0.0.1:17092 --log-dir tools/gtproxy/captures"

Clients need no changes (hosts file -> server IP as usual): HTTPS goes straight to
the server, ENet goes client -> proxy -> server. Watch live traffic:

    tmux attach -t gtproxy          # Ctrl-b d to detach
    tmux capture-pane -t gtproxy -p # or read the pane non-interactively

Restore normal operation:

    tmux kill-session -t gtproxy
    ~/opt/gtps-restart.sh           # back on 17091

While the proxy is stopped in capture mode, **no client can play** (ENet is
unreachable) - that is the trade-off.

## Capture logs

`tools/gtproxy/captures/capture-<date>-<time>-connN.jsonl`, one JSON object per
message or lifecycle event:

    {"t": 1760723195.123, "conn": 1, "dir": "c2s"|"s2c", "ch": 0,
     "kind": "text"|"tank"|"raw", "len": 60, "hex": "04 00 00 00 ...",
     "detail": "tank type=0x03 ... punch=(44,37) payload=0"}
    {"t": ..., "conn": 1, "event": "open"|"close", ...}

Text payloads are logged the way the server sees them (c2s: last byte dropped,
newlines -> `|`). Tank varlists are decoded to `"Name"(args...)`.

Quick look:

    python3 - <<'EOF'
    import json
    for l in open("tools/gtproxy/captures/capture-....jsonl"):
        x = json.loads(l)
        if x.get("kind") == "text": print(x["dir"], x["detail"][:300])
    EOF

## Limits / next steps

- HTTPS is not proxied (ENet captures are the goal; the login-size quirk is
  already understood - see docs/DEV-NOTES.md).
- Retail-client interop should be confirmed once with a real phone session (the
  ENet profile mirrors the game server and simulators pass end-to-end).
- Capturing feature flows (trade, ...) may first require the server to *offer*
  those dialogs (e.g. a wrench "Trade" button), otherwise the client has no way
  to open the UI whose packets we want to see.
