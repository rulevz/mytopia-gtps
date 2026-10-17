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

## Security
- NEVER commit: `resources/ctx/server.crt|key` (real TLS material), `items.dat`
  (game data), DB credentials, signing keystore, DuckDNS/other tokens, `LOCAL-RUN.md`.
- The debug logging currently prints login payloads; disable/redact before a public
  deployment (see TODO.md P0).
