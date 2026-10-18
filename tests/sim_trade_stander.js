// Phone-flow driver: one client (uid 1, the account whose phone plays) trades with the
// LIVE stander bot (uid 2) sitting in the same world - the exact path the phone uses.
// The stander is expected to (a) put 1050 x5 on the table by itself on OnStartTrade and
// (b) auto-accept ~3.5s after we accept, so the trade completes and items move both ways.
// Usage: node sim_trade_stander.js   (env: HOST/PORT/WORLD/UIDA/UIDB)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "TRADEDEMO";
const UIDA = parseInt(process.env.UIDA || "1", 10);
const UIDB = parseInt(process.env.UIDB || "2", 10);

function textPacket(peer, text) {
  const payload = Buffer.concat([Buffer.from([0x02, 0, 0, 0]), Buffer.from(text, "utf8"), Buffer.from([0])]);
  peer.send(new Packet(new Uint8Array(payload), PacketKind.Reliable));
}

const wait = (ms) => new Promise((r) => setTimeout(r, ms));

function mkClient(name, uid) {
  const state = { name, uid, replies: [], peer: null, client: null };
  state.client = new NodeENetClient(HOST, 0, 1, 2);
  state.send = (text) => textPacket(state.peer, text);
  state.client.emitter.on("connect", (peer) => {
    state.peer = peer;
    const parts = [
      "requestedName|", "f|1", "protocol|226", "game_version|5.57", "cbits|1040",
      "player_age|18", "GDPR|1", "FCMToken|t", "category|_-5100", "totalPlaytime|0",
      "klv|1", "gid|g", "tr|1", "meta|gurotopia", "fhash|0", "rid|0",
      "platformID|4", "deviceVersion|0", "country|us", "hash|0",
      "mac|00:00:00:00:00:00", "wk|NONE0", "lmode|1", "user|" + uid, "UUIDToken|0", "doorID|0", "aat|2"
    ];
    textPacket(peer, parts.join("\n") + "\n");
  });
  state.client.emitter.on("receive", (peer, packet) => {
    try {
      const d = Buffer.from(typeof packet.data === "function" ? packet.data() : packet.data);
      state.replies.push(d.toString("latin1"));
    } catch (e) { state.replies.push("<unparsed>"); }
  });
  return state;
}

function extractNetId(replies, uid) {
  for (const r of replies) {
    const m = r.match(new RegExp("netID\\|(\\d+)\\nuserID\\|" + uid + "\\n"));
    if (m) return parseInt(m[1], 10);
  }
  return 0;
}

const checks = [];
function check(name, cond) { checks.push([name, !!cond]); console.log((cond ? "CHECK PASS" : "CHECK FAIL") + ": " + name); }

(async () => {
  await init();
  const A = mkClient("A", UIDA);
  await A.client.start();
  A.client.startPolling(15);
  A.client.connect(HOST, PORT, 2, 0);
  console.log(`connecting A (uid ${UIDA}) to ${HOST}:${PORT}, world ${WORLD}`);

  await wait(2500);
  A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(2500);

  const bNetId = extractNetId(A.replies, UIDB);
  console.log(`stander netid in ${WORLD}: ${bNetId}`);
  check(`A sees the stander in ${WORLD}`, bNetId > 0);
  if (!bNetId) { console.log("RESULT: stander not found - is it running in this world?"); process.exit(1); }

  console.log("[A] wrench the stander");
  A.send(`action|wrench\nnetid|${bNetId}\n`);
  await wait(900);
  check("wrench dialog has a trade button", A.replies.some((r) => r.includes("add_button|trade")));

  A.send(`action|dialog_return\ndialog_name|popup\nnetID|${bNetId}\nbuttonClicked|trade\n`);
  await wait(1200);
  check("A got OnStartTrade", A.replies.some((r) => r.includes("OnStartTrade")));

  console.log("[A] offer 1050 x2 (stack -> quantity dialog -> answer)");
  A.send("action|mod_trade\nitemID|1050\n");
  await wait(700);
  A.send("action|dialog_return\ndialog_name|trade_item\nitemID|1050\ncount|2\n");
  await wait(900);
  check("A's own offer shows add_slot|1050|2", A.replies.some((r) => r.includes("add_slot|1050|2")));
  check("stander offered 1050 x5 by itself", A.replies.some((r) => r.includes("add_slot|1050|5")));

  await wait(1200); // @note the server gates accept for ~1s after an offer change
  console.log("[A] lock + accept; the stander should accept back");
  A.send("action|trade_lock\nstatus|1\n");
  await wait(500);
  A.send("action|trade_accept\nstatus|1\n");
  await wait(6000);
  check("trade completed (A got OnForceTradeEnd)", A.replies.some((r) => r.includes("OnForceTradeEnd")));
  check("A told the trade completed", A.replies.some((r) => r.includes("Trade completed")));

  const failed = checks.filter(([, ok]) => !ok).length;
  console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed, packets A=${A.replies.length}`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });