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

// 60-byte tank packet; pos/punch in their wire units (pos px, punch tiles)
function tankPacket(peer, type, id, posX, posY, punchX, punchY, state) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;      // packet_create
  b.writeInt32LE(type, o); o += 4;
  b.writeInt32LE(0, o); o += 4;      // netid
  b.writeInt32LE(0, o); o += 4;      // uid
  b.writeInt32LE(state, o); o += 4;
  b.writeFloatLE(0, o); o += 4;      // count
  b.writeInt32LE(id, o); o += 4;     // active hand
  b.writeFloatLE(posX, o); o += 4;
  b.writeFloatLE(posY, o); o += 4;
  b.writeFloatLE(0, o); o += 4;      // speed.x
  b.writeFloatLE(0, o); o += 4;      // speed.y
  b.writeFloatLE(0, o); o += 4;      // idk
  b.writeInt32LE(punchX, o); o += 4; // punch = last tapped tile (-1 when none)
  b.writeInt32LE(punchY, o); o += 4;
  b.writeUInt32LE(0, o); o += 4;     // size
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  let sawNetId = false;
  let announced = false;
  let sentAccept = false;
  let answeredDialog = false;
  let answeredConfirm = false;

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
      const m = s.match(new RegExp("netID\\|(\\d+)\\nuserID\\|" + UID + "\\n[\\s\\S]*?posXY\\|(\\d+)\\|(\\d+)\\n"));
      if (m && !sawNetId) {
        sawNetId = true;
        console.log(">> my netID in world:", m[1], "- stand here so others can wrench you");
      }
      // announce our position like a real client does (walk-mode packet, punch -1):
      // without this the server only knows the join-time baseline position.
      if (m && !announced) {
        announced = true;
        // stand one tile to the right of the spawn door so players spawning on the
        // door tile see you next to them instead of overlapping
        const px = (parseInt(m[2], 10) + 1) * 32, py = parseInt(m[3], 10) * 32;
        setTimeout(() => {
          tankPacket(peer, 0x00, 0, px, py, -1, -1, 0x04);
          console.log(">> position announced at", (parseInt(m[2], 10) + 1) + "," + m[3]);
        }, 600);
      }
      // auto-accept trades: when the partner's offer panel shows they accepted,
      // accept back after a short delay so the full trade cycle can complete
      if (!sentAccept && s.includes("'s offer") && !s.includes("Your offer") && s.includes("accepted|1")) {
        sentAccept = true;
        console.log(">> partner accepted - accepting back in 3.5s");
        setTimeout(() => {
          textPacket(peer, "action|trade_accept\nstatus|1\n");
          console.log(">> auto-accepted the trade");
        }, 3500);
      }
      // put a stack on the table too, so phone trades actually receive something.
      // tops itself up first (tester is DEVELOPER) - the stock drains ~3 per trade.
      if (s.includes("OnStartTrade")) {
        sentAccept = false; answeredDialog = false; answeredConfirm = false; // fresh trade, fresh flags
        setTimeout(() => {
          console.log(">> topping up + offering 1050 x5");
          textPacket(peer, "action|input\ntext|/give 1050 5\n");
          setTimeout(() => textPacket(peer, "action|mod_trade\nitemID|1050\n"), 600);
        }, 1500);
      }
      // the server asks for confirmation when we tap accept - answer it
      if (!answeredConfirm && s.includes("end_dialog|trade_accept")) {
        answeredConfirm = true;
        setTimeout(() => {
          textPacket(peer, "action|dialog_return\ndialog_name|trade_accept\nbuttonClicked|Accept\n");
          console.log(">> confirmed accepting the trade");
        }, 400);
      }
      if (!answeredDialog && s.includes("end_dialog|trade_item")) {
        answeredDialog = true;
        setTimeout(() => {
          textPacket(peer, "action|dialog_return\ndialog_name|trade_item\nitemID|1050\ncount|5\n");
          console.log(">> offer confirmed (1050 x5)");
        }, 800);
      }
    } catch (e) {}
  });

  client.connect(HOST, PORT, 2, 0);
  console.log(`stander: uid ${UID} -> ${HOST}:${PORT}, world ${WORLD}, staying ${STAY}s`);

  setInterval(() => console.log(`stander: alive at ${new Date().toISOString()}${sawNetId ? "" : " (not in world yet?)"}`), 60000);
  setTimeout(() => { console.log("stander: done"); process.exit(0); }, STAY * 1000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
