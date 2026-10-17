// Stage-1 login simulator (protocol|226 ltoken).
// Mimics what Growtopia clients send on their first ENet connection.
// Usage: npm i growtopia.wasm && node sim_protocol_login.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;

// ltoken = base64 of "_token=<...>&growId=<name>&password=<pw>&reg=<0|1>"
const b64 = Buffer.from("_token=abc&growId=tester&password=test1234&reg=0").toString("base64");

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  client.emitter.on("connect", (peer) => {
    console.log(">> ENet connected, sending protocol login packet");
    const text = "protocol|226\nltoken|" + b64 + "\n";
    const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
    peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
  });

  client.emitter.on("receive", (peer, packet, channel) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      console.log("<< recv", d.length, "bytes:", d.slice(0, 60).toString("hex"));
    } catch (e) { console.log("<< recv (unparsed)"); }
  });

  client.emitter.on("disconnect", (peer, data) => {
    console.log(">> disconnected, code", data);
    process.exit(0);
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting to", HOST + ":" + PORT, "...");
  setTimeout(() => { console.log("timeout, exiting"); process.exit(0); }, 9000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
