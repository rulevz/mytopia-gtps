// Minimal session-lifetime probe: connect as one uid, join the world, stay connected,
// send a heartbeat every 3s, print every reply (with a count) and the wall-clock lifetime.
// Usage: UID=2 WORLD=TRADEDEMO PORT=17091 node probe_session.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "TRADEDEMO";
const UID = parseInt(process.env.UID || "2", 10);
const SECS = parseInt(process.env.SECS || "25", 10);

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

(async () => {
  await init();
  const t0 = Date.now();
  const st = { replies: 0, names: new Set(), peer: null };
  const client = new NodeENetClient(HOST, 0, 1, 2);
  client.emitter.on("connect", (peer) => {
    st.peer = peer;
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + UID, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
    console.log(`[${((Date.now()-t0)/1000).toFixed(1)}s] sent login`);
  });
  client.emitter.on("disconnect", (peer, data) => {
    console.log(`[${((Date.now()-t0)/1000).toFixed(1)}s] DISCONNECTED by server (data=${data}) after ${st.replies} replies`);
  });
  client.emitter.on("receive", (peer, packet) => {
    st.replies++;
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      const s = d.toString("latin1");
      const m = s.match(/`6>> `?Welcome|OnSuperMainStartAcceptLogon|OnSetCurrentWorld|OnSpawn/);
      if (m) console.log(`[${((Date.now()-t0)/1000).toFixed(1)}s] reply#${st.replies}: ${m[0].slice(0, 40)}`);
    } catch (e) {}
  });
  await client.start(); client.startPolling(15);
  client.connect(HOST, PORT, 2, 0);

  await wait(2500);
  console.log(`[${((Date.now()-t0)/1000).toFixed(1)}s] sending join ${WORLD}`);
  textPacket(st.peer, `action|join_request\nname|${WORLD}\ninvis|0\n`);

  for (let i = 0; i < SECS; i += 3) {
    await wait(3000);
    if (st.peer) { try { textPacket(st.peer, "action|ping\n"); } catch (e) { console.log("send failed:", e.message); } }
    console.log(`[${((Date.now()-t0)/1000).toFixed(1)}s] alive, replies=${st.replies}`);
  }
  console.log(`RESULT: uid ${UID} stayed connected ${SECS}s, replies=${st.replies}`);
  process.exit(0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
