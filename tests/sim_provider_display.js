// Provider + Display Block end-to-end (5.57 tile extras).
//
// STEP=1  fresh world: /give ATM Machine + Display Block + an item to display,
//         place the ATM  -> tile update 0x05 with extra type 0x09 and growTime = 0
//         punch the ATM  -> REFUSED ("needs more time to grow"), no drop
//         wrench the Display Block -> dialog with an item picker (owner only)
//         answer the picker -> tile update 0x05 with extra type 0x17 + item id
//         rejoin -> world-data stream still parses to exact EOF (see
//                   tests/world_stream_check.py, run separately on the capture)
// STEP=2  after the DB provider tick has been pushed into the past + server restart:
//         punch the ATM -> gems drop (0x0d inv update) and the tile update carries a
//         RESET growTime (~0), proving the harvest clock persists and re-arms.
//
// Usage: PORT=17091 node sim_provider_display.js      (env: HOST/PORT/WORLD/STEP)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "PROVIDER1";
const STEP = process.env.STEP || "1";
const TX = parseInt(process.env.TX || "50", 10);
const TY = parseInt(process.env.TY || "20", 10);
const UID = parseInt(process.env.UID || "3", 10); // sim3

const ATM = 1008;
const DISPLAY_BLOCK = 2946;
const DISPLAY_ITEM = 334; // Art Wall (decorative, not a lock/door)
const DIRT = 2;

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

// 60-byte tank: type @4, id @24, punch @48/52
function tankPacket(peer, type, id, px, py) {
  const b = Buffer.alloc(60);
  b.writeInt32LE(4, 0);
  b.writeInt32LE(type, 4);
  b.writeInt32LE(id, 24);
  b.writeFloatLE((px + 1) * 32, 28);
  b.writeFloatLE(py * 32, 32);
  b.writeInt32LE(px, 48);
  b.writeInt32LE(py, 52);
  b.writeUInt32LE(0, 56);
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

function mkClient(uid) {
  const s = { uid, replies: [], tanks: [], peer: null, client: null };
  s.send = (t) => textPacket(s.peer, t);
  s.tank = (type, id, x, y) => tankPacket(s.peer, type, id, x, y);
  s.client = new NodeENetClient(HOST, 0, 1, 2);
  s.client.emitter.on("connect", (peer) => {
    s.peer = peer;
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + uid, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
  });
  s.client.emitter.on("receive", (peer, packet) => {
    const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
    s.replies.push(d.toString("latin1"));
    if (d.length >= 4 && d[0] === 4) s.tanks.push(Buffer.from(d));
  });
  return s;
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

const tType = (b) => b.readInt32LE(4);
const tPunchX = (b) => b.readInt32LE(48);
const tPunchY = (b) => b.readInt32LE(52);

// tile update (0x05): fg i16@60, bg i16@62, state[4]@64..67, [parent u16], type u8, extra
function tileUpdate(b) {
  const fg = b.readInt16LE(60);
  const bg = b.readInt16LE(62);
  const state2 = b[66];
  let off = 68;
  if (state2 & 0x02) off += 2;
  const type = b[off];
  off += 1;
  let extra = null;
  if (type === 0x09) extra = { kind: "provider", growTime: b.readUInt32LE(off) };
  else if (type === 0x17) extra = { kind: "display", itemID: b.readInt32LE(off) };
  else if (type === 0x18) extra = {
    kind: "vending",
    itemID: b.readInt32LE(off), price: b.readInt32LE(off + 4),
    stock: b.readUInt32LE(off + 8), wl: b.readUInt32LE(off + 12)
  };
  return { fg, bg, state2, type, extra, x: tPunchX(b), y: tPunchY(b) };
}

function worldDataPackets(s, from) {
  // world-data is a text 0x02 packet whose payload starts with the world header, or a
  // compressed tank. We only need to know it arrived; the byte-level check is done by
  // tests/world_stream_check.py on a dump.
  return s.replies.slice(from).filter((r) => r.includes("OnSpawn") || r.includes("spawn|"));
}

(async () => {
  await init();
  const A = mkClient(UID);
  await A.client.start();
  A.client.startPolling(15);
  A.client.connect(HOST, PORT, 2, 0);
  check("connected", await until(() => A.peer !== null, 5000));
  await wait(400);
  A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  check("joined world", await until(() => A.replies.some((r) => r.includes("entered")), 5000));
  await wait(500);

  /* ---------- STEP 1 ---------- */
  if (STEP === "1") {
    A.send(`action|input\ntext|/give ${ATM} 1\n`); await wait(500);
    A.send(`action|input\ntext|/give ${DISPLAY_BLOCK} 1\n`); await wait(500);
    A.send(`action|input\ntext|/give ${DISPLAY_ITEM} 1\n`); await wait(900);
    check("items granted", A.replies.filter((r) => r.includes("Gave")).length >= 1);

    // ---- place the ATM ----
    const t0 = A.tanks.length;
    A.tank(3, ATM, TX, TY);
    await wait(800);
    const atmTiles = A.tanks.slice(t0).filter((b) => tType(b) === 0x05)
      .map(tileUpdate).filter((t) => t.x === TX && t.y === TY && t.fg === ATM);
    check("ATM tile update received", atmTiles.length > 0);
    const atm = atmTiles[atmTiles.length - 1];
    check("ATM extra type = 0x09 (provider)", !!atm && atm.type === 0x09);
    check("ATM growTime starts at 0", !!atm && atm.extra && atm.extra.growTime === 0);

    // ---- harvest too early -> refused, nothing drops ----
    const t1 = A.tanks.length;
    A.tank(3, 18, TX, TY); // punch
    await wait(900);
    const refusals = A.replies.slice(-12).filter((r) => r.includes("needs more time to grow"));
    check("early harvest refused with grow text", refusals.length > 0);
    const drops = A.tanks.slice(t1).filter((b) => tType(b) === 0x0d || tType(b) === 0x0e);
    check("early harvest dropped nothing", drops.length === 0);

    // ---- display block: place it on empty ground, then wrench it ----
    A.tank(3, DISPLAY_BLOCK, TX + 1, TY);
    await wait(700);

    const r0 = A.replies.length;
    A.tank(3, 32, TX + 1, TY); // wrench the display block
    await wait(900);
    const dlg = A.replies.slice(r0).find((r) => r.includes("display_block"));
    check("display block dialog opened", !!dlg);
    check("dialog offers an item picker", !!dlg && dlg.includes("add_item_picker|displayitem"));

    // ---- answer the picker ----
    const t2 = A.tanks.length;
    A.send(`action|dialog_return\ndialog_name|display_block\ntilex|${TX + 1}\ntiley|${TY}\ndisplayitem|${DISPLAY_ITEM}\n`);
    await wait(900);
    const dispTiles = A.tanks.slice(t2).filter((b) => tType(b) === 0x05)
      .map(tileUpdate).filter((t) => t.x === TX + 1 && t.y === TY);
    const disp = dispTiles[dispTiles.length - 1];
    check("display block tile update received", dispTiles.length > 0);
    check("display extra type = 0x17", !!disp && disp.type === 0x17);
    check(`display extra carries item ${DISPLAY_ITEM}`, !!disp && disp.extra && disp.extra.itemID === DISPLAY_ITEM);

    // ---- the item left the backpack (it is on display now) ----
    check("inventory update after displaying", A.tanks.slice(t2).some((b) => tType(b) === 0x0d));
  }

  /* ---------- STEP 2 ---------- */
  if (STEP === "2") {
    const t0 = A.tanks.length;
    A.tank(3, 18, TX, TY); // punch the ATM
    await wait(1200);

    const gems = A.tanks.slice(t0).filter((b) => tType(b) === 0x0d || tType(b) === 0x0e);
    check("harvest dropped gems", gems.length > 0);

    const after = A.tanks.slice(t0).filter((b) => tType(b) === 0x05)
      .map(tileUpdate).filter((t) => t.x === TX && t.y === TY && t.fg === ATM);
    const last = after[after.length - 1];
    check("harvest re-armed the clock (growTime ~0)", !!last && last.extra && last.extra.growTime < 60);

    // and it is refused again right after harvesting
    const t1 = A.tanks.length;
    A.tank(3, 18, TX, TY);
    await wait(900);
    check("second harvest immediately refused", A.replies.slice(-10).some((r) => r.includes("needs more time to grow")));
    check("second harvest dropped nothing", A.tanks.slice(t1).filter((b) => tType(b) === 0x0d || tType(b) === 0x0e).length === 0);
  }

  console.log("SUMMARY:", checks.every(([, c]) => c) ? "ALL PASS" : "FAILURES",
    "(" + checks.filter(([, c]) => c).length + "/" + checks.length + ")");
  process.exit(checks.every(([, c]) => c) ? 0 : 1);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
