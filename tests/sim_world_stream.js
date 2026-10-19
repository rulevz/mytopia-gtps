// World-stream + tile-extra verification against the LIVE server.
//
//   GROWID=sim3 UID=3 WORLD=ZTILEEXTRA node sim_world_stream.js
//
// 1. joins the world and dumps the PACKET_SEND_MAP_DATA payload to a file, so
//    tests/world_stream_check.py can assert the bytes the client actually receives
//    parse to exact EOF (tile-extra widths are correct on the wire, not just in a dump).
// 2. punches the provider tile   -> expects a "needs more time" refusal (harvest gate)
// 3. wrenches the display block  -> expects the display_block dialog
const fs = require("fs");
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const UID = parseInt(process.env.UID || "3", 10);
const WORLD = process.env.WORLD || "ZTILEEXTRA";
const PX = parseInt(process.env.PX || "5", 10);   // provider tile
const PY = parseInt(process.env.PY || "20", 10);
const DX = parseInt(process.env.DX || "6", 10);   // display block tile
const DY = parseInt(process.env.DY || "20", 10);
const OUT = process.env.OUT || "/tmp/world_stream_live.bin";

const checks = [];
function check(name, ok, detail) {
  checks.push({ name, ok, detail });
  console.log(`${ok ? "PASS" : "FAIL"}  ${name}${detail ? "  (" + detail + ")" : ""}`);
}

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

function tileAction(peer, px, py, id) {
  const b = Buffer.alloc(60);
  b.writeInt32LE(4, 0);             // packet_create
  b.writeInt32LE(3, 4);             // type: tile_change
  b.writeInt32LE(id, 24);           // id: 18 = fist/punch, 32 = wrench (NOT @20 = count)
  b.writeFloatLE((px + 1) * 32, 28); // pos.x pixels
  b.writeFloatLE(py * 32, 32);      // pos.y pixels
  b.writeInt32LE(px, 48);           // punch.x
  b.writeInt32LE(py, 52);           // punch.y
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

let mapBytes = 0;
const texts = [];

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  client.emitter.on("connect", (peer) => {
    textPacket(peer, [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + UID,
      "UUIDToken|0", "doorID|0", "aat|2"
    ].join("\n") + "\n");

    setTimeout(() => textPacket(peer, "action|join_request\nname|" + WORLD + "\ninvis|0\n"), 1200);
    setTimeout(() => { console.log(">> punch provider at", PX, PY); tileAction(peer, PX, PY, 18); }, 3000);
    setTimeout(() => { console.log(">> wrench display at", DX, DY); tileAction(peer, DX, DY, 32); }, 3800);
  });

  client.emitter.on("receive", (peer, packet) => {
    const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
    if (d.length >= 60 && d.readInt32LE(0) === 4) {
      const type = d.readInt32LE(4);
      const state = d.readInt32LE(16);
      if (type === 4 && (state & 8)) {          // PACKET_SEND_MAP_DATA + S_EXTENDED
        const payload = d.subarray(60);
        fs.writeFileSync(OUT, payload);
        mapBytes = payload.length;
        console.log(`<< map data: ${payload.length} bytes -> ${OUT}`);
        return;
      }
    }
    const s = d.toString("latin1");
    if (s.startsWith("On")) {
      texts.push(s);
      console.log("<<", JSON.stringify(s.replace(/[^\x20-\x7e\n]/g, "·")).slice(0, 300));
    }
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting uid", UID, "->", WORLD);

  setTimeout(() => {
    check("map data received", mapBytes > 40000, mapBytes + " bytes");
    check("provider harvest gated",
      texts.some(t => /needs more time to grow/i.test(t)),
      texts.find(t => /needs more time/i.test(t)) || "no refusal text");
    check("display block dialog",
      texts.some(t => t.includes("OnDialogRequest") && t.includes("display_block")),
      texts.some(t => t.includes("display_block")) ? "dialog_name=display_block" : "not seen");

    const failed = checks.filter(c => !c.ok).length;
    console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed`);
    process.exit(failed ? 1 : 0);
  }, 6000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
