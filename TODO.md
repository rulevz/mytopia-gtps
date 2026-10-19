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
- [x] World lifecycle fixed: an exit can arrive twice (main door fires tile_activate ->
      quit_to_exit, and the client also sends action|quit_to_exit) - the second call used to
      underflow `visitors`, delete the world under remaining players and reset netids to 1
      (colliding avatars -> players turned invisible). Now: idempotent exit guard, safe
      visitor decrement, collision-free netid assignment (u_short counter); regression test
      `tests/sim_world_door_exit.js` (6/6, also verified through the capture proxy)
- [x] Avatar visibility/position sync fixed (all three layers): (1) spawn broadcasts use
      `peer::pos` - now initialized to the world spawn at join (idle peers used to spawn
      at tile 0,0, effectively invisible near the door); (2) `display_growid` fallback for
      clients that skip enter_game; (3) the tank punch bounds-guard now only applies to
      packets that INDEX blocks with punch (tile_change/tile_activate) - it was silently
      dropping ~63% of real-client movements (punch=-1 when walking), freezing avatars for
      everyone else. Regression: `tests/sim_movement_sync.js` (2/2 through the proxy)

## P1 - Gameplay content
- [~] `@todo` sweep: duct-tape 10min expiry (main-loop watchdog), object stacking cap 200 +
      overflow stacks, access list written to map data, tile refresh on block destruction,
      rare-item purchase text. Remaining (needs RE/wikidata): display-block dialog,
      provider countdown, map header bytes 0x19/0x40
- [x] Worlds: save/load + autosave; world lock flows complete (place claims ownership,
      wrench LockEdit dialog, public/private, access list 20 slots, entry gate) - verified
      end-to-end via tests/sim_lock_flow.js (owner claim / blocked entry / access grant /
      remove / public; all persisted through DB reload)
- [x] Commands: /give, /setrole, /kick, /ban, /unban, /mute with role gates (MODERATOR+/DEVELOPER+);
      role/banned/muted persisted in DB (startup migrations). Verified via tests/sim_command.js
- [x] Chat & moderation: mute + word filter (resources/chat_filter.txt - edit bebas) + [admin] audit log
- [~] Store/economy: gems persisted (starter 1000 + /setgems), store tabs & purchases verified
      (E4: World Lock purchase deducts & persists); real-money IAP packs still stubbed
- [x] Verify item rendering with the served items.dat (0x1a) on 5.57 — trade panels render
      real item icons/names from our file; the hash/deflate delivery fix below removed the
      "updating items" round-trip on every login
- [x] Trade system: display VERIFIED on a real 5.57 client — wrench→Trade opens both offer
      panels; the status format was recovered by disassembling libgrowtopia.so:
      `OnTradeStatus(netid, captionA, captionB, data)` where netid selects the panel (own vs
      partner) and data = `add_slot|<itemID>|<count>` lines + `locked|`/`accepted|` flags.
      Tapping a stack now asks how many: `trade_item` dialog (same shape as the proven drop
      dialog, count pre-filled with what the player holds, clamped server-side on return);
      untradeable items (CAT_UNTRADEABLE, e.g. 9640 My First World Lock) are refused with
      "You can't trade that.". COMPLETION MOVES ITEMS: on dual-accept the offered stacks are
      debited/credited between the two backpacks (clamped to what is still held + the
      200-per-entry cap), verified end-to-end through the proxy and persisted in the DB
      (uid 1: 22 -2 +5 = 25). Retail accept rules (mirrored from keichira/GTopia
      PlayerTrade): both directions are validated at completion (every offer must still be
      backed by the sender's backpack; the receiver must be able to hold it — 200/entry),
      a refusal tells BOTH players why and moves nothing; accepting is gated ~1s after an
      offer change (anti-scam swap); an offer change under an accepted deal sends
      "The deal has changed." + the tile_removed SFX; a cancel tells the other side who
      walked away. Disconnect/leave ends an active trade before the peer object dies. RETAIL
      FLOW (ported from the ReU/GrowTavern server source): both sides TICK accept -> the trade
      windows close (OnForceTradeEnd both) and each side gets the "Trade Confirmation" dialog
      (You'll give / You'll get rows + SCAM WARNING when receiving nothing; buttons
      accept="Do The Trade!" / back="Cancel", dialog_name trade_confirm). Nothing moves until
      BOTH pressed "Do The Trade!" (peer_trade.confirm = accept_the_offer); then the payout
      re-validates and moves each stack per offer. THE FLYING ITEMS (the retail "item melayang")
      are a 56-byte PlayerMoving broadcast per stack: type=0x03000013 (u8 type=19,
      punchRange=3), netid=receiver, target(+8)=sender, info=duration in ms, punchX=punchY=
      itemID; flights are staggered 450ms across both directions with change_clothes.wav landed
      at each step, followed by keypad_hit.wav at 1250ms and a world-wide
      "X traded N Item to Y." announcement. Cancel "back" / trade_cancel / leave sends both
      sides "Trade canceled by X!" (console+overlay) + dialog_cancel SFX. SFX on
      every event (play_sfx text action, names from the reference audio set): page_turn on
      the confirm dialog, use_lock on lock, bloop on confirmed accept, success on completion,
      dialog_cancel on cancel/leave, bleep_fail on refusals. ANTI-EXPLOIT HARDENING: new offer
      stacks cap at 4 (the client's status parser walks slotWidgets[i] unbounded - a 5th stack
      would run off the array), trade ops are flood-guarded (~12/s per peer), accept flags on
      BOTH sides drop when completion validation refuses, offers are cleared after payout so no
      later path can re-pay them, and input stays locked during the completion window. Dedicated
      test accounts: sims now run as uid3 (sim3) / uid4 (sim4) so a leftover stander or the dev
      phone session can never be bound as the trade partner. Tests: tests/sim_trade_protect.js
      (11 checks: slot cap, over-offer clamp, flood guard, double-accept race) and
      tests/probe_session.js (single-session lifetime probe). The confirm dialog shows one
      icon row per offered stack (add_label_with_icon small: real item icon + count). Heavy
      widgets (add_button_with_icon tiles, add_player_info blocks) are structurally valid -
      client parser minimums recovered from disassembly (>=6 / >=5 fields) - but looked
      cluttered inside a small dialog, so they stay out. Leftovers (non-blocking):
      `reset_locks|1` (retail servers never send it either) and `trade_check_price` (no
      reference reply exists — logged only)
- [x] Range locks (small/big/huge/builder's): retail-ported claim algorithm + full flow
      (rebuilt from GTopia's `WorldTileManager::ApplyLockTiles`) — nearest-eligible-tile
      scan with connected-area constraint and growing radius, max tiles 10/48/200/200,
      "ignore empty air"; governed tiles carry the lock's flat index as their parent
      (`state[0..1]`, `state[2]` bit 0x02 = HAS_PARENT, echoed a second time on the client
      wire); lock extra carries flags/owner/access; wrench dialog `tile_lock_edit`
      (access list, public, ignore-empty, Builder's Lock build-only/admins-limited,
      "Re-apply lock" recalc); SEND_LOCK (0x0f) marks the governed set for the world;
      smashing releases the area and returns the lock. Verified end-to-end by
      tests/sim_lock_range.js across a server restart (13+18 checks). docs/PROTOCOL.md
      updated with the SEND_LOCK field mapping + tile parent/flag rows
- [x] Golden Booty Chest loot tables: full drop list for Heartstone/GBC/Super GBC taken
      from growtopiawiki.com/w/Golden_Booty_Chest (69 pool rows + 13 Super-only items with
      names matched 1:1 against our v5.57 items.dat). Heartstone + GBC share the pool;
      Super GBC (9350) adds its exclusives and rolls the Golden Heart Crystal at 4x
      (verified 4x ratio by weight). Weighted roll with our own rarity tiering
      (`include/automate/loot.cpp`; retail weights are server-side). Stack counts follow
      the wiki (x10 blocks/lollipops, x2 loveseat/curtain). 1 free Super GBC per 100
      smashed GBCs (wiki, now GBC-only instead of counting all chests). Sim:
      tests/sim_gbc.js - 30 GBC + 5 Super + 5 Heartstone smashed, 40/40 drops, every id
      in the allowed set, x10/x2 counts exact (5/5 checks)

## P2 - Reverse engineering track (keep us up-to-date)
- [x] RE items.dat `0x1b` (clients 5.58+): trailing len-prefixed string + `==`->`>=` 0x1a gate
      fix; verified by exact-EOF parse of real v5.59 (16434 items). Server boots both formats
- [x] Version matrix + validation tool: docs/ITEMSDAT-FORMATS.md + tests/itemsdat_check.py
- [x] items.dat delivery fixed — no more "updating items" on every login: logon advertises
      the rttex hash (`h=0x55555555; h=(h>>27)+(h<<5)+byte`, uint32 wrap) of the bytes we
      ACTUALLY serve (computed at startup, after the .mp3-audio patch), and the refresh payload
      is DEFLATE-compressed (5.70 -> 0.98 MB) as tank type 0x10 / netid -1 / S_EXTENDED. The
      tank header layout was then verified against growtopia.js's TankPacket (the modern 60-byte
      header: netid@8, target@12, state@16, **info@24**, pos@28, speed@36, punch@48,
      dataLength@56): `info` (= our `gamePacket.id`) must carry the UNCOMPRESSED length, while
      `dataLength` (= our `.size`) is the compressed payload length. With info left at 0 the
      client discarded every payload, kept its own cache and repeated the local update step —
      the phone captures proved it (client `fhash` stayed put, no refresh request ever settled).
      The client's update path is the CDN URL from the logon varlist, not only the ENet ask:
      we were advertising the retail Akamai CDN, whose path 404s, so the update could never
      complete. Now the varlist points at OUR OWN address (gServer_data.server) and the https
      listener serves the raw bytes for any path containing `items.dat` (verified: 200 +
      5697998 bytes whose rttex hash equals the advertised 658714645). Verifying a fix: the
      client's login `fhash` should flip to the advertised hash, and `refresh_item_data`
      (logged per growid) should stop appearing
- [~] Packet captures for unimplemented features (trade, redeem codes, clash, gacha...)
      via `tools/gtproxy/` (our own ENet relay - replaces the GTProxy/packet-reader
      idea): built + verified end-to-end (login/commands/punch relayed byte-identically;
      jsonl captures + text/tank/varlist decode). Next: real-phone capture session, and
      fudge dialogs (e.g. wrench->Trade) so the client can open those UIs at all
- [x] Protocol cheat-sheet: `docs/PROTOCOL.md` (HTTPS discovery, ENet framing, tank
      layout + type tables, variant RPC wire format, full login flow, pool inventories,
      extension recipes) - speeds up the packet-capture / trade work below
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
