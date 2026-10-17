// Punch a tile a few times: exercises block damage + destruction + drop paths.
// Usage: GROWID=Twice UID=1 [WORLD=..] [TX=44] [TY=37] node sim_punch.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;
const UID = parseInt(process.env.UID || "1", 10);
const WORLD = process.env.WORLD || "TESTPUNCH";
const TX = parseInt(process.env.TX || "44", 10);
const TY = parseInt(process.env.TY || "37", 10);

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

function punchPacket(peer, px, py) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;      // packet_create
  b.writeInt32LE(3, o); o += 4;      // type: tile_change
  b.writeInt32LE(0, o); o += 4;      // netid
  b.writeInt32LE(0, o); o += 4;      // uid
  b.writeInt32LE(0, o); o += 4;      // state
  b.writeFloatLE(0, o); o += 4;      // count
  b.writeInt32LE(18, o); o += 4;     // id: fist (punch)
  b.writeFloatLE((px + 1) * 32, o); o += 4; // pos.x
  b.writeFloatLE(py * 32, o); o += 4;       // pos.y
  b.writeFloatLE(0, o); o += 4;      // speed.x
  b.writeFloatLE(0, o); o += 4;      // speed.y
  b.writeFloatLE(0, o); o += 4;      // idk
  b.writeInt32LE(px, o); o += 4;     // punch.x
  b.writeInt32LE(py, o); o += 4;     // punch.y
  b.writeUInt32LE(0, o); o += 4;     // size
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  let packets = 0;
  client.emitter.on("connect", (peer) => {
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + UID, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
    setTimeout(() => textPacket(peer, "action|join_request\nname|" + WORLD + "\ninvis|0\n"), 1200);
    for (let i = 0; i < 5; i++)
      setTimeout(() => { console.log(">> punch", i + 1, "at", TX, TY); punchPacket(peer, TX, TY); }, 2600 + i * 300);
  });
  client.emitter.on("receive", () => { packets++; });

  client.connect(HOST, PORT, 2, 0);
  setTimeout(() => { console.log("RESULT: packets=" + packets); process.exit(packets > 12 ? 0 : 1); }, 6000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
