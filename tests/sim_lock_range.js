// Range-lock (small/big/huge/builder's) end-to-end driver.
//
// STEP=1  fresh world: place 3 dirt blocks around the lock spot, place a Small Lock,
//         assert the SEND_LOCK broadcast (10 tiles, dirt tiles included), the
//         "Area locked." bubble + the lock tile update with its extra (owner/flags),
//         then force a save via a tile_lock_edit dialog return.
//         -> restart the server, then run STEP=2.
// STEP=2  after restart: the lock survived (B without access is denied with the retail
//         bubble and cannot damage), owner dialog still shows access list + options,
//         recalc with "Ignore empty air" shrinks the SEND_LOCK set to solid tiles only,
//         public flag lets B build, turning it off denies again, owner smash releases
//         every tile (tile updates) and drops the lock back (inventory update).
//
// Usage: PORT=17092 node sim_lock_range.js        (env: HOST/PORT/WORLD/STEP/TX/TY)
//
// NOTE: STEP=2 expects uid 4 to be a plain PLAYER (role 0) - staff bypass range
// locks on purpose, and the sim accounts are usually DEVELOPERs for /give.
// Prescription: node STEP=1  ->  UPDATE peer SET role=0 WHERE uid=4  ->  restart
// server  ->  node STEP=2  ->  UPDATE peer SET role=2 WHERE uid=4 (restore).
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "LOCKRANGE1";
const STEP = process.env.STEP || "1";
const TX = parseInt(process.env.TX || "50", 10);
const TY = parseInt(process.env.TY || "20", 10);

const UIDA = 3; // sim3 (owner)
const UIDB = 4; // sim4 (visitor)

const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const cord = (x, y) => y * 100 + x;

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

// 60-byte tank. type = TANK type (@4), id = item/fist(18)/wrench(32) (@24), punch = tile (@48/52)
function tankPacket(peer, type, id, px, py) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;      // packet_create (NET_MESSAGE_GAME_PACKET)
  b.writeInt32LE(type, o); o += 4;   // type
  b.writeInt32LE(0, o); o += 4;      // netid
  b.writeInt32LE(0, o); o += 4;      // uid
  b.writeInt32LE(0, o); o += 4;      // state
  b.writeFloatLE(0, o); o += 4;      // count
  b.writeInt32LE(id, o); o += 4;     // id (held item / 18 punch / 32 wrench)
  b.writeFloatLE((px + 1) * 32, o); o += 4; // pos.x pixels
  b.writeFloatLE(py * 32, o); o += 4;       // pos.y
  b.writeFloatLE(0, o); o += 4;      // speed.x
  b.writeFloatLE(0, o); o += 4;      // speed.y
  b.writeFloatLE(0, o); o += 4;      // idk
  b.writeInt32LE(px, o); o += 4;     // punch.x
  b.writeInt32LE(py, o); o += 4;     // punch.y
  b.writeUInt32LE(0, o); o += 4;     // size
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

function mkClient(name, uid) {
  const state = { name, uid, replies: [], tanks: [], peer: null, client: null };
  state.client = new NodeENetClient(HOST, 0, 1, 2);
  state.send = (text) => textPacket(state.peer, text);
  state.tank = (type, id, x, y) => tankPacket(state.peer, type, id, x, y);
  state.mark = () => state.replies.length;
  state.markTanks = () => state.tanks.length;
  state.client.emitter.on("connect", (peer) => {
    state.peer = peer;
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + uid, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
  });
  state.client.emitter.on("receive", (peer, packet) => {
    const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
    state.replies.push(d.toString("latin1"));
    if (d.length >= 4 && d[0] === 4) state.tanks.push(Buffer.from(d));
  });
  return state;
}

const checks = [];
function check(name, cond) {
  checks.push([name, !!cond]);
  console.log((cond ? "CHECK PASS" : "CHECK FAIL") + ": " + name);
}
async function until(fn, ms = 4000, step = 150) {
  const end = Date.now() + ms;
  while (Date.now() < end) { if (fn()) return true; await wait(step); }
  return fn();
}

/* ---- helpers over received tanks ---- */
const tType = (b) => b.readInt32LE(4);
const tNetid = (b) => b.readInt32LE(8);
const tId = (b) => b.readInt32LE(24);
const tPunchX = (b) => b.readInt32LE(48);
const tPunchY = (b) => b.readInt32LE(52);

function findTanks(state, type, from = 0) {
  return state.tanks.slice(from).filter((b) => tType(b) === type);
}
// SEND_LOCK (0x0f) payload: u16 tile indices
function lockTiles(b) {
  const size = b.readUInt32LE(56);
  const out = [];
  for (let i = 0; i < size / 2; i++) out.push(b.readUInt16LE(60 + i * 2));
  return out;
}
// tile update (0x05) payload: i16 fg, i16 bg, 4 state bytes, [u16 parent if HAS_PARENT], u8 type, extra
function tileUpdate(b) {
  const fg = b.readInt16LE(60);
  const bg = b.readInt16LE(62);
  const state2 = b[66];
  let off = 68;
  let parent = 0;
  if (state2 & 0x02) { parent = b.readUInt16LE(68); off += 2; }
  return { fg, bg, state2, parent, type: b[off], x: tPunchX(b), y: tPunchY(b) };
}

(async () => {
  await init();
  const A = mkClient("A", UIDA);
  const B = mkClient("B", UIDB);
  await A.client.start(); A.client.startPolling(15);
  console.log(`connecting A(uid ${UIDA}) to ${HOST}:${PORT}`);

  if (STEP === "1") {
    await B.client.start(); B.client.startPolling(15);
    A.client.connect(HOST, PORT, 2, 0);
    B.client.connect(HOST, PORT, 2, 0);
    check("A connected", await until(() => A.peer !== null, 5000));
    check("B connected", await until(() => B.peer !== null, 5000));
    await wait(500);
    A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
    check("A joined " + WORLD, await until(() => A.replies.some((r) => r.includes("entered"))));

    // B joins too + gets dirt now - while still DEVELOPER (uid4 is demoted to
    // PLAYER before STEP=2 so lock rules actually apply to B).
    B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
    await wait(700);
    B.send("action|input\ntext|/give 2 2\n");
    await wait(600);
    check("B pre-granted dirt", B.replies.some((r) => r.includes("Gave")));

    A.send("action|input\ntext|/give 202 1\n");
    await wait(600);
    A.send("action|input\ntext|/give 2 3\n");
    await wait(600);
    check("lock + dirt granted", A.replies.some((r) => r.includes("Gave")));

    // 3 dirt blocks around the lock spot (49,36) (51,36) (50,35)
    for (const [x, y] of [[TX - 1, TY], [TX + 1, TY], [TX, TY - 1]]) {
      A.tank(3, 2, x, y);
      await wait(350);
    }
    await wait(600);

    const tanksBefore = A.markTanks();
    A.tank(3, 202, TX, TY);
    await wait(900);

    check("area-locked bubble", A.replies.some((r) => r.includes("Area locked.")));
    const sl = findTanks(A, 0x0f, tanksBefore);
    check("A got a SEND_LOCK", sl.length > 0);
    if (sl.length > 0) {
      const b = sl[sl.length - 1];
      const tiles = lockTiles(b);
      check("SEND_LOCK netid=uidA id=202 at (TX,TY)", tNetid(b) === UIDA && tId(b) === 202 && tPunchX(b) === TX && tPunchY(b) === TY);
      check("SEND_LOCK governs 10 tiles", tiles.length === 10);
      check("dirt tiles inside the claim", [cord(TX - 1, TY), cord(TX + 1, TY), cord(TX, TY - 1)].every((c) => tiles.includes(c)));
    }
    const tu = findTanks(A, 0x05, tanksBefore).find((b) => b.readInt16LE(60) === 202);
    check("lock tile update received", !!tu);
    if (tu) {
      const t = tileUpdate(tu);
      check("lock extra: type=3 owner=uidA (count 0)", t.type === 3 && tu.readInt32LE(70) === UIDA);
    }

    // force persistence through the dialog save path
    A.send(`action|dialog_return\ndialog_name|tile_lock_edit\ntilex|${TX}\ntiley|${TY}\ncheckbox_public|0\ncheckbox_ignore|0\naccess|\nplayerNetID|\n`);
    check("dialog save persisted", await until(() => A.replies.some((r) => r.includes("saved."))));

    console.log("STEP 1 SUMMARY:", checks.every(([, c]) => c) ? "ALL PASS" : "FAILURES", "(" + checks.filter(([, c]) => c).length + "/" + checks.length + ")");
    process.exit(checks.every(([, c]) => c) ? 0 : 1);
  }

  if (STEP === "2") {
    await B.client.start(); B.client.startPolling(15);
    A.client.connect(HOST, PORT, 2, 0);
    B.client.connect(HOST, PORT, 2, 0);
    check("A connected", await until(() => A.peer !== null, 5000));
    check("B connected", await until(() => B.peer !== null, 5000));
    await wait(800);
    A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
    await wait(700);
    B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
    await until(() => A.replies.some((r) => r.includes("entered")) && B.replies.some((r) => r.includes("entered")), 4000);
    check("both joined after restart", A.replies.some((r) => r.includes("entered")) && B.replies.some((r) => r.includes("entered")));

    // B (no access) punches a dirt tile governed by the lock
    let m = B.mark();
    B.tank(3, 18, TX - 1, TY);
    await wait(700);
    check("B denied with retail bubble",
      B.replies.slice(m).some((r) => r.includes("Small Lock") && r.includes("No access")));

    // owner dialog survived (lock extra parsed from DB)
    m = A.mark();
    A.tank(3, 32, TX, TY);
    await wait(700);
    const dlg = A.replies.slice(m).join("\n");
    check("owner dialog: tile_lock_edit + Ignore empty air + Re-apply lock",
      dlg.includes("tile_lock_edit") && dlg.includes("Ignore empty air") && dlg.includes("Re-apply lock"));
    check("owner dialog lists access placeholder", dlg.includes("Access list:"));

    // add B to the access list
    m = A.mark();
    A.send(`action|dialog_return\ndialog_name|tile_lock_edit\ntilex|${TX}\ntiley|${TY}\naccess|sim4\n`);
    await wait(700);
    check("B added to access", A.replies.slice(m).some((r) => r.includes("added to the lock's access list")));

    // B with access: punch allowed (no denial) + can build inside the area
    m = B.mark();
    B.tank(3, 18, TX - 1, TY);
    await wait(500);
    const withAccess = B.replies.slice(m).join("\n");
    check("B (access) punch allowed - no denial", !withAccess.includes("No access"));

    const aTanks = A.markTanks();
    B.tank(3, 2, TX - 2, TY);
    await wait(700);
    const built = findTanks(A, 3, aTanks).some((b) => tPunchX(b) === TX - 2 && tPunchY(b) === TY && tId(b) === 2);
    check("B (access) can build inside the lock", built);

    // recalc with "Ignore empty air": only solid tiles stay governed
    m = A.markTanks();
    A.send(`action|dialog_return\ndialog_name|tile_lock_edit\ntilex|${TX}\ntiley|${TY}\ncheckbox_ignore|1\nbuttonClicked|recalc_lock\n`);
    await wait(800);
    const sl2 = findTanks(A, 0x0f, m);
    check("recalc sent a fresh SEND_LOCK", sl2.length > 0);
    if (sl2.length > 0) {
      const tiles = lockTiles(sl2[sl2.length - 1]);
      check("ignore-air: exactly the 4 solid tiles stay governed", tiles.length === 4 &&
        [cord(TX - 1, TY), cord(TX + 1, TY), cord(TX, TY - 1), cord(TX - 2, TY)].every((c) => tiles.includes(c)));
    }

    // public flag on + B removed from access -> B may still build (public)
    m = A.mark();
    A.send(`action|dialog_return\ndialog_name|tile_lock_edit\ntilex|${TX}\ntiley|${TY}\ncheckbox_public|1\naccess|sim4\n`);
    await wait(700);
    check("B removed from access", A.replies.slice(m).some((r) => r.includes("no longer on the lock's access list")));
    m = B.mark();
    B.tank(3, 18, TX - 1, TY);
    await wait(600);
    check("public lock: B allowed", !B.replies.slice(m).join("\n").includes("No access"));

    // public off -> denied again
    A.send(`action|dialog_return\ndialog_name|tile_lock_edit\ntilex|${TX}\ntiley|${TY}\ncheckbox_public|0\n`);
    await wait(600);
    m = B.mark();
    B.tank(3, 18, TX - 1, TY);
    await wait(600);
    check("private again: B denied", B.replies.slice(m).some((r) => r.includes("No access")));

    // owner smashes the lock (60 hits = the item's hp)
    m = A.markTanks();
    for (let i = 0; i < 62; i++) { A.tank(3, 18, TX, TY); await wait(45); }
    await wait(1000);

    const released = findTanks(A, 0x05, m).map(tileUpdate).filter((t) => t.x !== TX || t.y !== TY);
    check("smash released tiles (tile updates)", released.length >= 3);
    check("released tiles have no parent flag", released.every((t) => t.parent === 0 && (t.state2 & 0x02) === 0));
    // the smashed lock becomes a CAT_RETURN drop -> an item-change-object (0x0e) spawns
    const drop = findTanks(A, 0x0e, m);
    check("lock dropped as a returnable object (0x0e)", drop.length > 0);

    m = B.mark();
    B.tank(3, 18, TX - 1, TY);
    await wait(600);
    check("area free after smash: B allowed", !B.replies.slice(m).join("\n").includes("No access"));

    console.log("STEP 2 SUMMARY:", checks.every(([, c]) => c) ? "ALL PASS" : "FAILURES", "(" + checks.filter(([, c]) => c).length + "/" + checks.length + ")");
    process.exit(checks.every(([, c]) => c) ? 0 : 1);
  }

  console.log("unknown STEP " + STEP);
  process.exit(1);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
