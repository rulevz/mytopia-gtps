# Dev notes - running the server on a plain (rootless) Ubuntu VPS

This fork carries the production patches + tooling used to run the server without
sudo and without docker. Everything lives in user-space under `~/opt`.

## Directory layout (~/opt)
- `miniforge3` + conda env `gtps` - g++/make (rootless C++ toolchain)
- `gtps-deps/root` - library prefix (OpenSSL, libmariadb, zlib extracted from Ubuntu .debs)
- `mariadb-root` + `mariadb-data` - user-space MariaDB server + datadir
- `apks/` - Android client workspace (XAPK -> merged -> patched -> signed)
- `APKEditor.jar`, `android-14/` (build-tools: zipalign/apksigner), `gtps-apk.keystore`
  (signing keystore - kept on the host only, never committed)

## Build
```bash
PATH=$HOME/opt/miniforge3/envs/gtps/bin:$PATH \
make -j$(nproc) \
  includes="-Iinclude -Ibuild/include -I$HOME/opt/gtps-deps/root/usr/include -I$HOME/opt/gtps-deps/root/usr/include/x86_64-linux-gnu" \
  libraries="-L./include/enet/lib -L$HOME/opt/gtps-deps/root/usr/lib/x86_64-linux-gnu -lssl -lcrypto -lmariadb -lenet -lm -lpthread -lz"
```
Requires `items.dat`, `server_data.php`, `mysql_login.txt` in the working directory
(auto-generated with defaults except items.dat - see the main README/upstream docs).

## Runtime
- tmux session `gtps` (windows: `db`, `server`, `web`), controlled by local helper
  scripts (`gtps-start.sh` / `gtps-restart.sh` / `gtps-stop.sh`)
- HTTPS listener must bind port 443 for retail clients. Rootless binding is enabled via
  `net.ipv4.ip_unprivileged_port_start=443` (sysctl). `GUROTOPIA_HTTPS_PORT` env can
  override the port for local testing.

## TLS (critical for modern clients)
- Clients (5.4x+) validate certificates. The upstream self-signed cert fails with
  `SSL alert number 46` and the client shows a generic "check your internet / VPN"
  style error.
- Working solution: Let's Encrypt **IP certificate** (`shortlived` profile, 6-day
  validity) issued via `acme.sh --alpn --tlsport 443 -d <IP> --cert-profile shortlived`
  (port 443 must be free during validation; acme.sh stops/starts nothing - use
  pre/post hooks). Renewal is automatic (acme.sh cron, 4x/day). Each renewal needs a
  ~30s window where the game server is stopped (hooks do it).
- The server must serve the FULL chain: we use `SSL_CTX_use_certificate_chain_file`
  (the upstream code loaded only the leaf, breaking chain building).
- Alternative for domain-based setups: any real cert (certbot/acme.sh DNS-01) for a
  hostname that fits the client patch slot (max 18 bytes, e.g. `name.duckdns.org`).

## Android client patching (short version)
1. Get the exact client version from apkpure.net (`d.apkpure.net/b/APK/<pkg>?versionCode=NNN`;
   versionCode is on the `/versions` page rows as `data-dt-version-code`).
2. The XAPK contains split APKs; merge to a universal APK with APKEditor (`m` mode).
3. In `lib/arm64-v8a/libgrowtopia.so` replace `www.growtopia1.com` and
   `www.growtopia2.com` (null-terminated, exactly 18 bytes - replacement must be
   <= 18 bytes, zero-padded) with your IP/domain. Both literals occur exactly once.
4. `zipalign -p -f 4`, then `apksigner sign` with your keystore.
5. Verify: `apksigner verify`, `aapt dump badging`, re-check strings in the final APK.

## Patch inventory (this fork)
- `https`: any-size request reads (upstream answered ONLY exactly-213-byte requests,
  silently dropping newer clients -> hangs at "Getting server address")
- `https`: full certificate chain serving (`use_certificate_chain_file`)
- `https`: `GUROTOPIA_HTTPS_PORT` env override (default 443)
- `https`: diagnostics logging (client IP, size, request line)
- `login`: route modern session payloads (starting `requestedName|`) to the session
  login handler; resolve the account via `user|<uid>` when no `tankIDName` field is
  present; logon_fail instead of undefined behavior when unresolved
- `login`: bounds-checked pipe parsing
- `enet`: diagnostics logging (connect/disconnect IPs, text/tank/varlist)
- `enet`: `GUROTOPIA_ENET_PORT` env override for the ENet bind port - the advertised
  port in `server_data.php` stays unchanged, so a capture proxy can own the public
  port (see "Packet capture" below)
- `items`: logon advertises the rttex hash (`h=0x55555555; h=(h>>27)+(h<<5)+byte`,
  uint32 wrap) of the bytes we actually serve, computed at startup after the .mp3-audio
  patch - a stale hardcoded hash made every client re-request the item data on EVERY
  login ("One moment, updating item data...")
- `items`: the `refresh_item_data` payload is now DEFLATE-compressed (5.70 -> 0.98 MB)
  with tank type 0x10 / netid -1 / S_EXTENDED, matching retail servers (a raw payload
  never settled the client cache); the request is also logged per growid
- `items`: the logon varlist's CDN host now points at this server and the https listener
  serves the raw bytes for any path containing `items.dat` (200 + exact length). Advertising
  the retail CDN (whose path 404s) made the client's item update impossible to complete -
  it showed "updating items" on every launch. The advertised rttex hash is computed over
  exactly the bytes served

## Packet capture (`tools/gtproxy`)

Our own ENet relay captures what clients send for features the server does not
implement yet (trade, redeem codes, clash, gacha, ...). It decodes text / tank /
variant-RPC messages to the console + `tools/gtproxy/captures/*.jsonl` and relays
byte-for-byte (verified: identical packets direct vs proxied).

Capture mode: server binds an alternate ENet port
(`GUROTOPIA_ENET_PORT=17092 ~/opt/gtps-restart.sh`) while `server_data.php` keeps
advertising 17091 = proxy (`--listen 17091 --backend 127.0.0.1:17092`); HTTPS stays
direct on 443. Full runbook + ENet interop notes (new-packet mode kid):
`tools/gtproxy/README.md`.

## Security
- NEVER commit: `resources/ctx/server.crt|key` (real TLS material), `items.dat`
  (game data), DB credentials, signing keystore, DuckDNS/other tokens, `LOCAL-RUN.md`.
- The debug logging is gated behind `--verbose` (quiet by default); auth failures are
  always logged.

## Auth, logging & ops (P0)
- Passwords: `sha256:` + sha256(growid + ":" + password), stored in `peer.password`
  (column auto-widened to VARCHAR(128) by a startup migration). Legacy plaintext
  rows upgrade on first successful login. Wrong password -> `logon_fail`.
- Logging: quiet by default; run `./main.out --verbose` for packet/request diagnostics.
- Autostart: `scripts/autostart-cron.sh` installs `@reboot` start + daily backup entries.
- Backup: `scripts/backup.sh` -> `~/backups/gtps-<ts>.tar.gz` (DB dump + items.dat +
  configs, keeps the last 14).
