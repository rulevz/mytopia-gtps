// Golden Booty Chest drop-table driver: place a chest, smash it (6 punches) and
// collect the 0x0e drop objects; verifies every drop belongs to the allowed set
// and that the stack counts match the wiki table (x10 / x2 rows).
//   GBC=N        regular chests (default 30)
//   SUPER=N      Super GBCs, id 9350 (default 5)
//   HEART=N      Heartstones, id 392 (default 5)
// Usage: WORLD=GBCDROP1 node sim_gbc.js
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const UID = parseInt(process.env.UID || "3", 10);
const WORLD = process.env.WORLD || "GBCDROP1";
const N = parseInt(process.env.GBC || "30", 10);
const N_SUPER = parseInt(process.env.SUPER || "5", 10);
const N_HEART = parseInt(process.env.HEART || "5", 10);

// allowed drop ids: GBC pool + Super exclusives (growtopiawiki.com/w/Golden_Booty_Chest)
const GBC_POOL = new Set([1458,362,366,1470,2384,2388,3396,4412,5648,5658,7818,7820,7822,7824,7826,7830,9324,10654,13144,14230,14236,15192,15196,2390,2396,3398,4416,4424,5644,5652,5656,9332,9334,9336,9338,9340,9342,10626,10632,10652,10662,11682,11684,11686,11692,13118,13130,13132,13150,13152,14242,14244,14252,364,384,386,388,390,3400,3404,3406,3408,4400,4418,4422,5660,5664,7808,7810]);
const SUPER_POOL = new Set([15188,13126,13124,13116,13128,11668,11664,11680,11678,14238,14232,15198,13114]);
const XL10 = new Set([390,3404,3408,4400,7808,7810]);
const XL2 = new Set([4418,5660]);

function textPacket(peer, text) {
  peer.send(new Packet(new Uint8Array(Buffer.concat([
    Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])
  ])), PacketKind.Reliable));
}

function tankPacket(peer, type, id, px, py) {
  const b = Buffer.alloc(60);
  let o = 0;
  b.writeInt32LE(4, o); o += 4;      // packet_create (NET_MESSAGE_GAME_PACKET)
  b.writeInt32LE(type, o); o += 4;   // type
  b.writeInt32LE(0, o); o += 4;      // netid
  b.writeInt32LE(0, o); o += 4;      // uid
  b.writeInt32LE(0, o); o += 4;      // state
  b.writeFloatLE(0, o); o += 4;      // count
  b.writeInt32LE(id, o); o += 4;     // id (item id to place / 18 punch)
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

const drops = [];        // {id, count}
const texts = [];

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
      console.log(">> join", WORLD);
      textPacket(peer, "action|join_request\nname|" + WORLD + "\ninvis|0\n");
    }, 1200);
    setTimeout(() => textPacket(peer, "action|input\ntext|/give 3402 " + (N + 4) + "\n"), 1350);
    setTimeout(() => textPacket(peer, "action|input\ntext|/give 9350 " + (N_SUPER + 2) + "\n"), 1500);
    setTimeout(() => textPacket(peer, "action|input\ntext|/give 392 " + (N_HEART + 2) + "\n"), 1650);

    // build the smash queue: regular chests (row 20), super (21), heartstone (22;
    // heartstone needs 10 hits per the wiki, the chests 5 - punch one extra)
    const queue = [];
    for (let i = 0; i < N; i++)       queue.push({ id: 3402, x: 20 + i, y: 20, punches: 6 });
    for (let i = 0; i < N_SUPER; i++) queue.push({ id: 9350, x: 20 + i, y: 21, punches: 6 });
    for (let i = 0; i < N_HEART; i++) queue.push({ id: 392,  x: 20 + i, y: 22, punches: 12 });

    let t = 2400;
    for (const task of queue) {
      const at = t;
      setTimeout(() => {
        tankPacket(peer, 3, task.id, task.x, task.y); // place
      }, at);
      for (let k = 0; k < task.punches; k++)
        setTimeout(() => tankPacket(peer, 3, 18, task.x, task.y), at + 200 + k * 130); // punch
      t += 1250;
    }
  });

  client.emitter.on("receive", (peer, packet) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      if (d.length >= 60 && d.readInt32LE(0) === 4) {
        const type = d.readInt32LE(4);
        if (type === 0x0e) drops.push({ id: d.readInt32LE(24), count: d.readFloatLE(20) });
      }
      const s = d.toString("latin1");
      if (/entered|Give|Golden Heart|World Locked|error/i.test(s)) texts.push(s.replace(/[^\x20-\x7e]/g, "·"));
    } catch (e) {}
  });

  client.connect(HOST, PORT, 2, 0);
  console.log("connecting uid", UID, "world", WORLD, "queue =", N, "gbc +", N_SUPER, "super +", N_HEART, "heartstone");

  const totalTasks = N + N_SUPER + N_HEART;
  const runTime = 2400 + totalTasks * 1250 + 4000;
  setTimeout(() => {
    // ---- checks ----
    let pass = 0, fail = 0;
    const check = (name, ok, detail) => {
      console.log((ok ? "  OK  " : "  FAIL") + "  " + name + (detail ? "  | " + detail : ""));
      ok ? pass++ : fail++;
    };

    const byId = {};
    for (const d of drops) (byId[d.id] = byId[d.id] || []).push(d.count);

    check("drops received >= smash count - slack", drops.length >= totalTasks - 6, `drops=${drops.length} tasks=${totalTasks}`);
    const badIds = [...new Set(drops.map((d) => d.id))].filter((id) => !GBC_POOL.has(id) && !SUPER_POOL.has(id));
    check("all drop ids in the allowed set", badIds.length === 0, "offenders=" + JSON.stringify(badIds));
    const superOnly = [...new Set(drops.map((d) => d.id))].filter((id) => SUPER_POOL.has(id) && !GBC_POOL.has(id));
    check("regular GBC never drops super exclusives", superOnly.length === 0 || N === 0, "super-only seen after regular too=" + JSON.stringify(superOnly));
    const badX10 = drops.filter((d) => XL10.has(d.id) && d.count % 10 !== 0);
    check("x10 rows drop in multiples of 10", badX10.length === 0, JSON.stringify(badX10.slice(0, 5)));
    const badX2 = drops.filter((d) => XL2.has(d.id) && d.count % 2 !== 0);
    check("x2 rows drop in multiples of 2", badX2.length === 0, JSON.stringify(badX2.slice(0, 5)));

    console.log("\n--- distribution (" + new Set(drops.map((d) => d.id)).size + " distinct ids) ---");
    for (const id of Object.keys(byId).sort((a, b) => byId[b].length - byId[a].length))
      console.log(`  ${id}\tx${byId[id].length}\tcounts=[${byId[id].join(",")}]`);

    console.log("\n--- text highlights ---");
    for (const t of texts.slice(0, 10)) console.log(" <<", t.slice(0, 200));

    console.log(`\nRESULT: pass=${pass} fail=${fail}`);
    process.exit(fail ? 1 : 0);
  }, runTime);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
