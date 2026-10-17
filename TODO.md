# TODO / Roadmap

Private-server project for Growtopia (based on [gurotopia/Gurotopia](https://github.com/gurotopia/Gurotopia)),
plus the patched Android client and the reverse-engineering tooling around it.

**Current target client: Growtopia 5.57** (items.dat format `0x1a`). Do NOT update the
client before the items parser is upgraded (see P2).

## Status (achieved)
- [x] Build & run on a rootless Ubuntu VPS (user-space deps, see `docs/DEV-NOTES.md`)
- [x] HTTPS login (TCP 443) + ENet game server (UDP) live, reachable from the internet
- [x] Real TLS certificate (Let's Encrypt IP cert, shortlived profile, auto-renew)
- [x] Modern client login flow working end-to-end (HTTPS server_data -> ENet protocol
      login -> reconnect -> modern session payload) - user can enter the game
- [x] Patched Android client (5.57, arm64) built + hosted for players
- [x] Integration test simulators + diagnostics logging

## P0 - Make it solid to actually play & operate
- [x] Autostart on VPS reboot: `scripts/autostart-cron.sh` installs the @reboot entry
- [x] **Password verification on login** (salted sha256; legacy plaintext auto-upgrades;
      password column widened by a startup migration)
- [x] Quiet logging by default; `--verbose` flag for diagnostics (auth failures always logged)
- [~] Multi-player: automated concurrency smoke test added (`tests/multi_client_smoke.js`);
      real-device pass still pending (2+ phones: movement, tile sync, chat, disconnect)
- [x] Daily backups: `scripts/backup.sh` (DB dump + items.dat + configs, keeps 14) + cron entry
- [~] Robustness: MariaDB connection watchdog + auto-reconnect added (30s ping in main loop);
      86 source `@todo` markers triaged - biggest: tile_change (19), world (13), wrench (5)

## P1 - Gameplay content
- [~] `@todo` sweep: duct-tape 10min expiry (main-loop watchdog), object stacking cap 200 +
      overflow stacks, access list written to map data, tile refresh on block destruction,
      rare-item purchase text. Remaining (needs RE/wikidata): range-locks, GBC reward list,
      display-block dialog, provider countdown, map header bytes 0x19/0x40
- [x] Worlds: save/load + autosave; world lock flows complete (place claims ownership,
      wrench LockEdit dialog, public/private, access list 20 slots, entry gate) - verified
      end-to-end via tests/sim_lock_flow.js (owner claim / blocked entry / access grant /
      remove / public; all persisted through DB reload)
- [x] Commands: /give, /setrole, /kick, /ban, /unban, /mute with role gates (MODERATOR+/DEVELOPER+);
      role/banned/muted persisted in DB (startup migrations). Verified via tests/sim_command.js
- [x] Chat & moderation: mute + word filter (resources/chat_filter.txt - edit bebas) + [admin] audit log
- [~] Store/economy: gems persisted (starter 1000 + /setgems), store tabs & purchases verified
      (E4: World Lock purchase deducts & persists); real-money IAP packs still stubbed
- [ ] Verify item rendering with the served items.dat (0x1a) on 5.57
- [ ] Trade system (packets exist in the retail protocol - needs RE/capture work)

## P2 - Reverse engineering track (keep us up-to-date)
- [ ] RE the items.dat `0x1b` format (client 5.58+/5.59+): diff new fields, upgrade parser
      (current decoder supports up to 0x1a; newer files crash the boot)
- [ ] Client version watch: monitor apkpure version list every release, maintain a
      compatibility matrix (client version <-> items format <-> server support)
- [ ] Packet captures for unimplemented features (trade, redeem codes, clash, gacha...)
      via the proxy tooling (GTProxy / packet reader)
- [ ] Write a protocol cheat-sheet from the source + community docs (login flow, tank
      packets, variants) to speed up future development
- [ ] Contribute fixes back upstream (gurotopia/Gurotopia PRs) where they are generic
      (e.g. any-size HTTPS reads, modern payload routing, chain-file serving)

## P3 - Client distribution & player tooling
- [ ] Automate the APK pipeline into a script: latest supported version -> apkpure XAPK ->
      APKEditor merge -> string patch -> zipalign -> apksigner -> publish + SHA256
- [ ] armeabi-v7a build for older devices (32-bit)
- [ ] PC client patcher (growtopia.exe hostname patch, like c0dn/Growtopia-Patcher)
- [ ] Player-facing status/instructions page improvements (auto version, status badge)

## P4 - Engineering / DevOps
- [ ] GitHub Actions: build ubuntu-latest (existing upstream workflow) + our patches;
      release binaries automatically
- [ ] Run integration tests in CI (`tests/` simulators against a spawned server)
- [ ] Bootstrap script for a fresh VPS (rootless stack: toolchain, deps, MariaDB, tmux)
- [ ] Logging framework: levels, file output, rotation
- [ ] Optional: Docker deployment path for people who have docker

## Ground rules
- Never commit secrets: TLS keys, keystore, DB creds, tokens, `LOCAL-RUN.md` (see `.gitignore`)
- Keep the patches in this fork clean & documented (`docs/DEV-NOTES.md` has the inventory)
- Test login flow with `tests/` simulators before blaming the client
