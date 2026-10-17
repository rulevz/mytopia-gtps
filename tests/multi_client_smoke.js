// Multi-client concurrency smoke test: two clients log in at the same time
// (modern session payload w/ user|<uid>), both must receive server varlists
// and the server must stay alive.
// Usage: npm i growtopia.wasm && node multi_client_smoke.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;

function mkClient(name, uid) {
  return new Promise(async (resolve) => {
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
        "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + uid, "UUIDToken|0", "doorID|0", "aat|2"
      ];
      const text = parts.join("\n") + "\n";
      const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text), Buffer.from([0])]);
      peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
    });
    client.emitter.on("receive", () => { packets++; });
    client.emitter.on("disconnect", (peer, data) => resolve({ name, uid, packets, disconnected: true }));

    client.connect(HOST, PORT, 2, 0);
    setTimeout(() => resolve({ name, uid, packets }), 6000);
  });
}

(async () => {
  await init();
  const [a, b] = await Promise.all([mkClient("A", 1), mkClient("B", 2)]);
  console.log("clientA (user=1):", JSON.stringify(a));
  console.log("clientB (user=2):", JSON.stringify(b));
  const ok = a.packets >= 2 && b.packets >= 2;
  console.log(ok ? "MULTI-CLIENT OK" : "MULTI-CLIENT FAILED");
  process.exit(ok ? 0 : 1);
})();
