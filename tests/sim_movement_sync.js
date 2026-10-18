// Regression: player movement must reach the server even when the client has no
// punch target (retail clients send punch=(-1,-1) while walking). The punch
// bounds guard used to drop those packets silently -> players appeared frozen
// for everyone else while walking without tapping (63% of real movements!).
// Scenario: A and B join WORLD; B walks (6x punch -1, 4x valid punch); A must
// receive ALL 10 movement broadcasts.
// Usage: node sim_movement_sync.js   (env: HOST/PORT/WORLD/UIDA/UIDB)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "MOVETEST";
const UIDA = parseInt(process.env.UIDA || "1", 10);
const UIDB = parseInt(process.env.UIDB || "2", 10);

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

function movementPacket(peer, posX, posY, punchX, punchY, state) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;
  b.writeInt32LE(0x00, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeInt32LE(state, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeFloatLE(posX, o); o += 4;
  b.writeFloatLE(posY, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeInt32LE(punchX, o); o += 4;
  b.writeInt32LE(punchY, o); o += 4;
  b.writeUInt32LE(0, o); o += 4;
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

function mkClient(name, uid) {
  const state = { name, uid, replies: [], raw: [], peer: null, client: null };
  state.client = new NodeENetClient(HOST, 0, 1, 2);
  state.send = (text) => textPacket(state.peer, text);
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
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      state.replies.push(d.toString("latin1"));
      state.raw.push(d); // raw bytes (tank packets are binary)
    } catch (e) { state.replies.push("<unparsed>"); }
  });
  return state;
}

function selfNetId(replies, uid) {
  for (const r of replies) {
    const m = r.match(new RegExp("netID\\|(\\d+)\\nuserID\\|" + uid + "\\n"));
    if (m) return parseInt(m[1], 10);
  }
  return 0;
}

const checks = [];
function check(name, cond) { checks.push([name, !!cond]); console.log((cond ? "CHECK PASS" : "CHECK FAIL") + ": " + name); }

(async () => {
  await init();
  const A = mkClient("A", UIDA);
  const B = mkClient("B", UIDB);
  await A.client.start(); A.client.startPolling(15);
  await B.client.start(); B.client.startPolling(15);
  A.client.connect(HOST, PORT, 2, 0);
  B.client.connect(HOST, PORT, 2, 0);
  console.log(`connecting A(uid ${UIDA}) and B(uid ${UIDB}) to ${HOST}:${PORT}, world ${WORLD}`);

  await wait(2500);
  A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(900);
  B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(2000);

  const bNetId = selfNetId(B.replies, UIDB);
  console.log("B netid:", bNetId);
  if (!bNetId) { console.log("no netid, aborting"); process.exit(1); }

  const markA = A.raw.length;
  console.log("[B] walking: 6x punch(-1,-1) + 4x valid punch");
  for (let i = 0; i < 10; i++) {
    const noPunch = i < 6;
    movementPacket(B.peer, 700 + i * 8, 1152, noPunch ? -1 : 30, noPunch ? -1 : 35, 0x24);
    await wait(120);
  }
  await wait(1500);

  const movements = A.raw.slice(markA).filter((b) =>
    b.length >= 60 && b[0] === 4 && b.readInt32LE(4) === 0x00 && b.readInt32LE(8) === bNetId);
  console.log(`A received ${movements.length}/10 movement broadcasts for B`);
  check("all 10 movements (incl. punch=-1) reached A", movements.length === 10);
  check("positions synced (>= 4 distinct)",
    new Set(movements.map((b) => b.readFloatLE(28))).size >= 4);

  const failed = checks.filter(([, ok]) => !ok).length;
  console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
