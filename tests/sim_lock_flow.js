// World-lock flow driver: join a world, place a lock, wrench it, submit the
// LockEdit dialog (public flag / access list). Steps selected via STEP env:
//   STEP=1  join + place World Lock (id 9640) at TX,TY
//   STEP=2  join + wrench the block (expect LockEdit dialog)
//   STEP=3  join + submit dialog_return lock_edit (env PUBLIC=0/1, ACCESS=name)
//   STEP=4  join only (check entry gate: entered / failed-locked)
// Usage: GROWID=Twice UID=1 STEP=1 node sim_lock_flow.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = "127.0.0.1";
const PORT = 17091;
const GROWID = process.env.GROWID || "Twice";
const PASSWORD = process.env.PASSWORD || "kamuko8799";
const UID = parseInt(process.env.UID || "1", 10);
const WORLD = process.env.WORLD || "TESTLOCK";
const STEP = process.env.STEP || "1";
const TX = parseInt(process.env.TX || "50", 10);
const TY = parseInt(process.env.TY || "36", 10);

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

// 60-byte tank/game packet (matches the server's ::gamePacket layout)
function tankPacket(peer, type, id, px, py) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;      // packet_create (NET_MESSAGE_GAME_PACKET)
  b.writeInt32LE(type, o); o += 4;   // type
  b.writeInt32LE(0, o); o += 4;      // netid
  b.writeInt32LE(0, o); o += 4;      // uid
  b.writeInt32LE(0, o); o += 4;      // state
  b.writeFloatLE(0, o); o += 4;      // count
  b.writeInt32LE(id, o); o += 4;     // id (held item / 32 wrench / 18 punch)
  b.writeFloatLE((px + 1) * 32, o); o += 4; // pos.x pixels (stand on neighbour tile)
  b.writeFloatLE(py * 32, o); o += 4;       // pos.y
  b.writeFloatLE(0, o); o += 4;      // speed.x
  b.writeFloatLE(0, o); o += 4;      // speed.y
  b.writeFloatLE(0, o); o += 4;      // idk
  b.writeInt32LE(px, o); o += 4;     // punch.x (tile coords - INT on the wire)
  b.writeInt32LE(py, o); o += 4;     // punch.y
  b.writeUInt32LE(0, o); o += 4;     // size
  peer.send(new Packet(new Uint8Array(b), PacketKind.Reliable));
}

(async () => {
  await init();
  const client = new NodeENetClient(HOST, 0, 1, 2);
  await client.start();
  client.startPolling(15);

  const replies = [];

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
      console.log(">> join", WORLD);
      textPacket(peer, "action|join_request\nname|" + WORLD + "\ninvis|0\n");
    }, 1200);

    setTimeout(() => {
      if (STEP === "1") {
        console.log(">> place lock at", TX, TY);
        tankPacket(peer, 3, 9640, TX, TY);
      } else if (STEP === "2") {
        console.log(">> wrench lock at", TX, TY);
        tankPacket(peer, 32, 0, TX, TY);
      } else if (STEP === "3") {
        const dialog = "action|dialog_return\ndialog_name|lock_edit\ntilex|" + TX + "\ntiley|" + TY +
          "\ncheckbox_public|" + (process.env.PUBLIC || "0") +
          "\ncheckbox_disable_music|0\nminimum_entry_level|1\naccess|" + (process.env.ACCESS || "") +
          "\nplayerNetID|\n";
        console.log(">> submit lock_edit dialog PUBLIC=" + (process.env.PUBLIC || "0") + " ACCESS=" + (process.env.ACCESS || ""));
        textPacket(peer, dialog);
      }
    }, 2600);
  });

  client.emitter.on("receive", (peer, packet) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      replies.push(d.toString("latin1"));
    } catch (e) {}
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting as", GROWID, "step", STEP);
  setTimeout(() => {
    const joined = replies.some((r) => r.includes("entered"));
    const failed = replies.some((r) => r.includes("FailedToEnterWorld") || r.includes("locked world"));
    const dialog = replies.some((r) => r.includes("LockEdit"));
    console.log("RESULT: packets=" + replies.length + " joined=" + joined + " failedLocked=" + failed + " lockDialog=" + dialog);
    for (const r of replies) {
      const s = r.replace(/[^\x20-\x7e]/g, "·");
      if (/entered|locked|LockEdit|access list|World Locked|PUBLIC|PRIVATE|Failed/.test(s)) console.log("<<", s.slice(0, 300));
    }
    process.exit(0);
  }, 6000);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
