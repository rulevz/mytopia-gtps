// Idle stander: logs in and stays in a world, so another player (e.g. a phone on
// the capture proxy) has someone to wrench for trade captures.
// Usage: NODE_PATH=<growtopia.wasm dir> UID=2 WORLD=TRADETEST STAY=1800 node sim_stander.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const UID = parseInt(process.env.UID || "2", 10);
const WORLD = process.env.WORLD || "TRADETEST";
const STAY = parseInt(process.env.STAY || "1800", 10);

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  let sawNetId = false;

  client.emitter.on("connect", (peer) => {
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + UID, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
    setTimeout(() => {
      console.log(">> joining", WORLD);
      textPacket(peer, `action|join_request\nname|${WORLD}\ninvis|0\n`);
    }, 1200);
  });

  client.emitter.on("receive", (peer, packet) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      const s = d.toString("latin1");
      const m = s.match(new RegExp("netID\\|(\\d+)\\nuserID\\|" + UID + "\\n"));
      if (m && !sawNetId) {
        sawNetId = true;
        console.log(">> my netID in world:", m[1], "- stand here so others can wrench you");
      }
    } catch (e) {}
  });

  client.connect(HOST, PORT, 2, 0);
  console.log(`stander: uid ${UID} -> ${HOST}:${PORT}, world ${WORLD}, staying ${STAY}s`);

  setInterval(() => console.log(`stander: alive at ${new Date().toISOString()}${sawNetId ? "" : " (not in world yet?)"}`), 60000);
  setTimeout(() => { console.log("stander: done"); process.exit(0); }, STAY * 1000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
