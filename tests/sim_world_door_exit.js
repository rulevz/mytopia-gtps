// Regression: a world exit must be idempotent. Tapping the main door fires
// tile_activate (server-side quit_to_exit) AND the client also sends
// action|quit_to_exit. The second call used to decrement the world's visitor
// count again -> the world got deleted while peers were still in it, netids
// reset to 1 and avatars collided (players became invisible).
// Scenario: A and B join WORLD; B uses the spawn door exactly like a real client
// (+ quit_to_exit) and rejoins. Expected: B keeps netid continuity, the "entered"
// count still lists A, A sees exactly one remove + B's new spawn.
// Usage: node sim_world_door_exit.js   (env: HOST/PORT/WORLD/UIDA/UIDB)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "EXITTEST";
const UIDA = parseInt(process.env.UIDA || "1", 10);
const UIDB = parseInt(process.env.UIDB || "2", 10);

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

// 60-byte tank packet (same writer as sim_lock_flow)
function tankPacket(peer, type, id, px, py) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;
  b.writeInt32LE(type, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeInt32LE(0, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeInt32LE(id, o); o += 4;
  b.writeFloatLE((px + 1) * 32, o); o += 4;
  b.writeFloatLE(py * 32, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeFloatLE(0, o); o += 4;
  b.writeInt32LE(px, o); o += 4;
  b.writeInt32LE(py, o); o += 4;
  b.writeUInt32LE(0, o); o += 4;
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

function mkClient(name, uid) {
  const state = { name, uid, replies: [], peer: null, client: null };
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
    } catch (e) { state.replies.push("<unparsed>"); }
  });
  return state;
}

/* own spawn: netID + tile coords (spawn tile == main door tile in generated worlds) */
function selfSpawn(replies, uid) {
  for (const r of replies) {
    const m = r.match(new RegExp("netID\\|(\\d+)\\nuserID\\|" + uid + "\\n[\\s\\S]*?posXY\\|(\\d+)\\|(\\d+)\\n"));
    if (m) return { netid: parseInt(m[1], 10), x: parseInt(m[2], 10), y: parseInt(m[3], 10) };
  }
  return null;
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

  const a1 = selfSpawn(A.replies, UIDA);
  const b1 = selfSpawn(B.replies, UIDB);
  console.log(`netid: A=${a1 && a1.netid} B=${b1 && b1.netid} (B spawn/door tile ${b1 && b1.x},${b1 && b1.y})`);
  check("both joined with distinct netids", !!(a1 && b1 && b1.netid > a1.netid));
  if (!a1 || !b1) process.exit(1);

  const markA = A.replies.length;
  const markB = B.replies.length;
  console.log("[B] tap main door (tank 0x07) + quit_to_exit -- the real double-exit");
  tankPacket(B.peer, 0x07, 0, b1.x, b1.y);
  await wait(150);
  B.send("action|quit_to_exit\n");
  await wait(1500);

  console.log("[B] rejoin " + WORLD);
  B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(2500);

  const b2 = selfSpawn(B.replies.slice(markB), UIDB);
  const entered2 = B.replies.slice(markB).find((r) => r.includes("other people here"));
  check("B got a NEW netid after rejoin (world not recreated -> no reset to 1)",
    !!(b2 && b2.netid > b1.netid));
  check("visitors still counted (entered says `w1`` other)", !!(entered2 && entered2.includes("`w1`` other")));
  check("B entered without failure", !B.replies.slice(markB).some((r) => r.includes("OnFailedToEnterWorld")));

  const aAfter = A.replies.slice(markA);
  const removes = aAfter.filter((r) => r.includes("OnRemove") && r.includes(`pId|${UIDB}`)).length;
  check("A saw exactly ONE remove for B (no double decrement)", removes === 1);
  check("A sees B again with the new netid",
    !!(b2 && aAfter.some((r) => r.includes(`netID|${b2.netid}\n`) && r.includes(`userID|${UIDB}`))));

  const failed = checks.filter(([, ok]) => !ok).length;
  console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed, packets A=${A.replies.length} B=${B.replies.length}`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
