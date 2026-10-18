# Integration tests / simulators

Small standalone simulators that exercise the server's login flow without a game
client. Useful to validate server patches quickly.

Requirements:
- python3 (HTTPS test)
- Node.js + `npm i growtopia.wasm` (ENet simulators; pure WASM, no native build)

Usage (server must be running; edit HOST/PORT at the top of each file if needed):
- `python3 https_login_test.py [port]`
  Sends a legacy 213-byte POST (size used by older clients) and prints the
  server_data reply. Modern clients send different sizes - the server must answer
  any of them.
- `node sim_protocol_login.js`
  Stage-1 login: sends `protocol|226\nltoken|<b64>` and expects the
  `OnSendToServer` varlist back.
- `node sim_modern_login.js`
  Stage-2 (modern clients 5.4x+): sends a session payload starting with
  `requestedName|` carrying `user|<uid>`, expects GDPR + OnSuperMainStart varlists.
- `node multi_client_smoke.js`
  Concurrency smoke: two clients log in simultaneously (modern payload); both must
  receive server varlists and the server must stay alive (crash regression guard).
- `node sim_lock_flow.js`
  World-lock flow driver (`STEP=1..4`, env `WORLD/TX/TY/PUBLIC/ACCESS`): place lock,
  wrench (LockEdit dialog), submit dialog (public/access), join-check. Used to verify
  ownership claim, entry gate and access list end-to-end.
- `python3 itemsdat_check.py file.dat [...]`
  items.dat format validator: mirrors the C++ parser (through 0x1b) and asserts EXACT
  end-of-file consumption + printable XOR-decoded names. Run before shipping a new file.
- `node sim_punch.js`
  Joins a world and punches a tile 5 times — exercises block damage, destruction, drops
  (object stacking) and the tank bounds guard.
- `node sim_command.js`
  Full login then sends one command/chat message
  (`GROWID=.. UID=.. CMD="/help" node sim_command.js`), prints every server reply —
  used to verify admin commands, role gates and moderation flows.
- `node sim_trade_flow.js`
  Two-client trade driver (uid 1 + 2, world TRADETEST): wrench → Trade button →
  quantity dialog → offer/removal → re-offer → lock/accept/complete (with item
  transfer) plus a cancel pass (incl. the "X has canceled the trade" overlay) —
  20 checks, against the capture proxy by default. Accept always asks for confirmation
  first (`trade_accept` dialog listing what you give/get; the sim answers it with
  `dialog_return|dialog_name|trade_accept|buttonClicked|Accept`). Accept is gated ~1s
  after an offer change (anti-scam), so leave >=1.2s between the last offer and the accept.
- `node sim_trade_stander.js`
  The phone flow against the LIVE stander bot: joins the stander's world, wrenches
  it, checks that the stander puts its own 1050 x5 on the table and auto-accepts,
  and that the trade completes — 8 checks. Use a world the bot can enter (a locked
  world refuses role-PLAYER peers with OnFailedToEnterWorld). To exercise the
  accept-time refusal, seed the bot's backpack full first:
  `UPDATE peer SET inventory = UNHEX('1000000001001A04C800') WHERE uid = 2;`
  (1050 x200) — the completion is then refused with "... is carrying too many ...".
- `node sim_stander.js`
  Idle stander bot: logs in (UID, default 2) and stays in a world (WORLD, default
  TRADETEST) so another player — e.g. a phone through the capture proxy — has
  someone to wrench. On trade start it offers its own 1050 x5 (the account is
  seeded with that stack) and auto-accepts ~3.5s after the partner accepts. STAY
  seconds (default 1800).
- `node sim_world_door_exit.js`
  Regression for the world-lifecycle fix: B taps the main door (tank 0x07) + sends
  quit_to_exit (the real double-exit) and rejoins — netid must not reset, visitors
  must stay counted, A must see exactly one remove + B's new spawn.
- `node sim_movement_sync.js`
  Regression for the punch-guard scope fix: B walks with punch=-1,-1 (what retail
  clients send while walking) + valid punch; A must receive ALL movement broadcasts
  (the guard used to silently drop ~63% of real movements - players appeared frozen).

Notes:
- `sim_protocol_login.js` registers/loads the account inside the ltoken payload
  (`growId=`, `password=` must be present in the base64).
- `sim_modern_login.js` needs an existing account with that uid (it is resolved via
  `SELECT growid FROM peer WHERE uid = ?`).
- All ENet simulators honor `HOST`/`PORT` env overrides (defaults: 127.0.0.1:17091) -
  e.g. `PORT=17191 node sim_command.js` through the capture proxy (`tools/gtproxy`).
  Beware of stale `HOST`/`PORT` exports in your shell silently overriding defaults.
