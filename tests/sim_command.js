// Log in (modern session payload) then send one in-game command/chat message.
// Usage:
//   GROWID=Twice PASSWORD=kamu8799 UID=1 CMD="/help" node sim_command.js
// Prints every packet the server sends back (ascii-ish).
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;
const GROWID = process.env.GROWID || "Twice";
const PASSWORD = process.env.PASSWORD || "kamu8799";
const UID = parseInt(process.env.UID || "1", 10);
const CMD = process.env.CMD || "/help";
const WAIT = parseInt(process.env.WAIT || "1600", 10);

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

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
      if (process.env.RAW) {
        console.log(">> sending RAW:", JSON.stringify(process.env.RAW));
        textPacket(peer, process.env.RAW);
      } else {
        console.log(">> sending:", CMD);
        textPacket(peer, "action|input\ntext|" + CMD + "\n");
      }
    }, WAIT);
  });

  client.emitter.on("receive", (peer, packet) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      const s = d.toString("latin1").replace(/[^\x20-\x7e\n]/g, "·");
      console.log("<<", JSON.stringify(s).slice(0, 400));
    } catch (e) { console.log("<< (unparsed packet)"); }
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting as", GROWID, "(uid", UID + ")");
  setTimeout(() => process.exit(0), WAIT + 3000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
