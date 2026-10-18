// Two-client trade flow driver for the capture-first trade implementation.
// Flow: both clients log in -> both join WORLD -> A wrenches B (player dialog with
// Trade button) -> A submits dialog_return popup/trade -> both expect OnStartTrade ->
// A offers items (mod_trade, rem_trade) -> B locks (trade_lock) -> both accept ->
// expect OnForceTradeEnd on both. Second pass: start again + trade_cancel.
// Usage: node sim_trade_flow.js   (env: HOST/PORT/WORLD/UIDA/UIDB)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "TRADETEST";
const UIDA = parseInt(process.env.UIDA || "3", 10);
const UIDB = parseInt(process.env.UIDB || "4", 10);

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

function extractNetId(clientsReplies, uid) {
  for (const r of clientsReplies) {
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
  const B = mkClient("B", UIDB);
  await A.client.start(); A.client.startPolling(15);
  await B.client.start(); B.client.startPolling(15);
  A.client.connect(HOST, PORT, 2, 0);
  B.client.connect(HOST, PORT, 2, 0);
  console.log(`connecting A(uid ${UIDA}) and B(uid ${UIDB}) to ${HOST}:${PORT}`);

  await wait(2500);
  console.log("[A] join " + WORLD); A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(900);
  console.log("[B] join " + WORLD); B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(2000);

  const bNetId = extractNetId(A.replies, UIDB);
  const aNetId = extractNetId(B.replies, UIDA);
  console.log(`netids: A=${aNetId} B=${bNetId} (A saw B=${bNetId > 0}, B saw A=${aNetId > 0})`);
  check("both saw each other spawn", bNetId > 0 && aNetId > 0);
  if (!bNetId || !aNetId) process.exit(1);

  // --- trade #1: full happy path ---
  console.log("[A] wrench B");
  A.send(`action|wrench\nnetid|${bNetId}\n`);
  await wait(900);
  check("A got wrench player dialog with trade button", A.replies.some((r) => r.includes("add_button|trade")));

  console.log("[A] tap Trade");
  A.send(`action|dialog_return\ndialog_name|popup\nnetID|${bNetId}\nbuttonClicked|trade\n`);
  await wait(900);
  check("A got OnStartTrade", A.replies.some((r) => r.includes("OnStartTrade")));
  check("B got OnStartTrade", B.replies.some((r) => r.includes("OnStartTrade")));
  check("A got OnTradeStatus", A.replies.some((r) => r.includes("OnTradeStatus")));

  console.log("[A] stocking up for the quantity-dialog test");
  A.send("action|input\ntext|/give 1050 5\n");
  await wait(800);
  check("give command replied", A.replies.some((r) => r.includes("Gave 5x item 1050")));

  console.log("[A] tap an untradeable item (9640 = My First World Lock) -> refusal");
  A.send("action|mod_trade\nitemID|9640\n");
  await wait(500);
  check("untradeable item refused", A.replies.some((r) => r.includes("You can't trade that")));

  console.log("[A] tap 1050 (stack) -> expect the quantity dialog");
  A.send("action|mod_trade\nitemID|1050\n");
  await wait(700);
  check("quantity dialog shown for a stack", A.replies.some((r) => r.includes("trade_item")));
  A.send("action|dialog_return\ndialog_name|trade_item\nitemID|1050\ncount|2\n");
  await wait(700);
  check("B sees A's offer add_slot|1050|2", B.replies.some((r) => r.includes("add_slot|1050|2")));

  console.log("[A] remove 1050 from the offer");
  const markB = B.replies.length;
  A.send("action|rem_trade\nitemID|1050\n");
  await wait(800);
  const afterRem = B.replies.slice(markB).join("\n"); // only packets sent after the removal
  check("B's view after removal has no 1050 offer", !afterRem.includes("add_slot|1050"));

  console.log("[A] re-offer 1050 x2 so the trade completes WITH an offer (item transfer)");
  const markB2 = B.replies.length;
  A.send("action|mod_trade\nitemID|1050\n");
  await wait(600);
  A.send("action|dialog_return\ndialog_name|trade_item\nitemID|1050\ncount|2\n");
  await wait(600);
  check("B sees the re-offer add_slot|1050|2",
    B.replies.slice(markB2).some((r) => r.includes("add_slot|1050|2")));

  await wait(1200); // @note the server gates accept for ~1s after an offer change
  console.log("[B] lock 1; both accept");
  B.send("action|trade_lock\nstatus|1\n");
  await wait(400);
  check("A sees partner locked|1", A.replies.some((r) => r.includes("locked|1")));

  console.log("[A] tap accept -> confirmation dialog -> confirm");
  A.send("action|trade_accept\nstatus|1\n");
  await wait(700);
  check("accept asks for confirmation",
    A.replies.some((r) => r.includes("end_dialog|trade_accept")) && A.replies.some((r) => r.includes("Accept this trade")));
  check("confirmation lists what A gives",
    A.replies.some((r) => r.includes("You give:") && !r.includes("You give:`` nothing")));
  A.send("action|dialog_return\ndialog_name|trade_accept\nbuttonClicked|Accept\n");
  await wait(500);

  console.log("[B] tap accept -> confirm too");
  B.send("action|trade_accept\nstatus|1\n");
  await wait(700);
  check("B asked for confirmation too", B.replies.some((r) => r.includes("end_dialog|trade_accept")));
  B.send("action|dialog_return\ndialog_name|trade_accept\nbuttonClicked|Accept\n");
  await wait(1200);
  check("mutual-accepted status pushed (animation cue)",
    A.replies.some((r) => r.includes("'s offer") && r.includes("accepted|1")));
  check("A told the trade completed", A.replies.some((r) => r.includes("Trade completed")));
  check("completion sfx played", A.replies.some((r) => r.includes("play_sfx") && r.includes("success.wav")));
  await wait(3200); // @note windows stay open ~3s so the client can animate the swap
  check("A got OnForceTradeEnd (completion)", A.replies.some((r) => r.includes("OnForceTradeEnd")));
  check("B got OnForceTradeEnd (completion)", B.replies.some((r) => r.includes("OnForceTradeEnd")));

  // --- trade #2: cancel path ---
  console.log("[A] start trade again, then cancel");
  A.send(`action|wrench\nnetid|${bNetId}\n`); await wait(400);
  A.send(`action|dialog_return\ndialog_name|popup\nnetID|${bNetId}\nbuttonClicked|trade\n`); await wait(600);
  A.send("action|trade_cancel\n"); await wait(700);
  check("B got a second OnForceTradeEnd (cancel)",
    B.replies.filter((r) => r.includes("OnForceTradeEnd")).length >= 2);
  check("B told who canceled", B.replies.some((r) => r.includes("has canceled the trade")));

  const failed = checks.filter(([, ok]) => !ok).length;
  console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed, packets A=${A.replies.length} B=${B.replies.length}`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
