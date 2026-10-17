// Stage-2 modern session login simulator (requestedName|... payload).
// Mimics what Growtopia 5.4x+ clients send on their SECOND ENet connection
// (after OnSendToServer). The account is resolved server-side via user|<uid>.
// Usage: npm i growtopia.wasm && node sim_modern_login.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  client.emitter.on("connect", (peer) => {
    console.log(">> connected, sending MODERN session payload (requestedName|...)");
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|test:fcm", "category|_-5100", "totalPlaytime|0",
      "klv|8F3EC8F6AD0236BFBE2A9BDB85DD3255", "gid|7e1c1f32-022a-4a21-96a8-dd9accb662ed",
      "tr|4322", "meta|gurotopia", "fhash|-716928004", "rid|0278D02A3272BB720757C0A9A9C272E5",
      "platformID|4", "deviceVersion|0", "country|us", "hash|-2005874635",
      "mac|02:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|1", "UUIDToken|0", "doorID|0", "aat|2"
    ];
    const text = parts.join("\n") + "\n";
    const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text), Buffer.from([0])]);
    peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
  });

  client.emitter.on("receive", (peer, packet, channel) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      console.log("<< recv", d.length, "bytes:", d.slice(0, 40).toString("hex"));
    } catch (e) { console.log("<< recv (unparsed)"); }
  });

  client.emitter.on("disconnect", (peer, data) => {
    console.log(">> disconnected, code", data);
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting to", HOST + ":" + PORT, "...");
  setTimeout(() => { console.log("timeout, exiting"); process.exit(0); }, 9000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
