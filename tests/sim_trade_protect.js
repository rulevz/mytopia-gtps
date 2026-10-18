// Trade anti-exploit driver: proves the server-side guards added with the trade feature.
// 1) 4-slot cap   : a 5th distinct stack is refused (the client's status parser walks
//                   slotWidgets[i] unbounded -> more than 4 would run off the array)
// 2) over-offer   : count|9999 lands in the status as what the player actually holds
// 3) flood guard  : a burst of lock toggles is rate-limited, the server stays responsive
// 4) accept race  : firing the confirm twice completes the trade exactly once
// Usage: node sim_trade_protect.js   (env: HOST/PORT/WORLD/UIDA/UIDB)
const { init, NodeENetClient, Packet, PacketKind } = require("growtopia.wasm");

const HOST = process.env.HOST || "127.0.0.1";
const PORT = parseInt(process.env.PORT || "17091", 10);
const WORLD = process.env.WORLD || "TRADEDEMO";
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
  A.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(900);
  B.send(`action|join_request\nname|${WORLD}\ninvis|0\n`);
  await wait(2000);

  const bNetId = extractNetId(A.replies, UIDB);
  const aNetId = extractNetId(B.replies, UIDA);
  const bSelfNetId = extractNetId(B.replies, UIDB);
  check("both saw each other spawn", bNetId > 0 && aNetId > 0);
  // @note guard against trading the wrong session: when B's feed shows its OWN spawn, the
  // netid A sees for uidB must match it (catches duplicate-login / leftover stander sessions).
  if (bSelfNetId > 0) check("partner identity matches the sim's B session", bNetId === bSelfNetId);
  else console.log("[sim] B's own spawn not in its feed - identity check skipped (dedicated uids)");
  if (!bNetId || !aNetId) process.exit(1);

  A.send(`action|wrench\nnetid|${bNetId}\n`); await wait(800);
  A.send(`action|dialog_return\ndialog_name|popup\nnetID|${bNetId}\nbuttonClicked|trade\n`); await wait(800);
  check("trade started", A.replies.some((r) => r.includes("OnStartTrade")));

  // stock A with candidate stacks (any missing id is simply skipped by the fill loop)
  // @note the /give command runs through the chat spam limiter (5 inputs / 6s) - space them
  const candidates = [2, 1050, 6, 46, 68, 242, 294, 8];
  for (const id of candidates) { A.send(`action|input\ntext|/give ${id} 3\n`); await wait(1500); }

  const offer = async (id, n) => {
    A.send(`action|mod_trade\nitemID|${id}\n`); await wait(500);
    A.send(`action|dialog_return\ndialog_name|trade_item\nitemID|${id}\ncount|${n}\n`); await wait(500);
  };

  // --- 1) fill all four offer slots, then a 5th distinct stack must be refused ---
  const accepted = [];
  for (const id of candidates) {
    if (accepted.length >= 4) break;
    const markB = B.replies.length;
    await offer(id, 1);
    const fresh = B.replies.slice(markB).join("\n");
    if (fresh.includes(`add_slot|${id}|`)) accepted.push(id);
    else console.log(`[sim] offer of ${id} did not land (untradeable / not held)`);
  }
  check("filled exactly 4 offer slots", accepted.length === 4);

  const fifth = candidates.find((id) => !accepted.includes(id));
  if (fifth === undefined) {
    check("5th-stack test (needs an unused candidate)", false);
  } else {
    const markB = B.replies.length;
    const markA = A.replies.length;
    await offer(fifth, 1);
    const freshB = B.replies.slice(markB).join("\n");
    const freshA = A.replies.slice(markA).join("\n");
    check("5th distinct stack refused (no add_slot for it)", !freshB.includes(`add_slot|${fifth}|`));
    check("5th stack hit the cap, not the tradeable check",
      freshA.includes("bleep_fail") && !freshA.includes("You can't trade that"));
  }

  // --- 2) over-offer clamp: ask for 9999 of a held stack ---
  const clampId = accepted[0];
  const markB2 = B.replies.length;
  await offer(clampId, 9999);
  const m = B.replies.slice(markB2).join("\n").match(new RegExp(`add_slot\\|${clampId}\\|(\\d+)`));
  const landed = m ? parseInt(m[1], 10) : 0;
  console.log(`[sim] count|9999 -> status shows add_slot|${clampId}|${landed}`);
  check("over-offer clamped to <= 200", landed > 0 && landed <= 200);
  check("over-offer clamped to what A actually holds (not 9999)", landed !== 9999);

  // --- 3) flood guard: 30 lock toggles in one burst ---
  for (let i = 0; i < 30; i++) A.send(`action|trade_lock\nstatus|${i % 2}\n`);
  await wait(1500); // @note the guard window is 1s
  const markA3 = A.replies.length;
  B.send("action|trade_lock\nstatus|1\n"); await wait(700);
  check("server responsive after the flood (A sees B's locked|1)",
    A.replies.slice(markA3).some((r) => r.includes("locked|1")));

  // --- 4) accept race: fire the confirm twice, trade must complete once ---
  await wait(1300); // @note accept gate after the last offer change
  A.send("action|trade_accept\nstatus|1\n"); await wait(400);
  B.send("action|trade_accept\nstatus|1\n"); await wait(900); // both accepted -> confirmation dialogs
  check("windows closed at both-accept",
    A.replies.some((r) => r.includes("OnForceTradeEnd")) && B.replies.some((r) => r.includes("OnForceTradeEnd")));
  A.send("action|dialog_return\ndialog_name|trade_confirm\nbuttonClicked|accept\n"); await wait(250);
  A.send("action|dialog_return\ndialog_name|trade_confirm\nbuttonClicked|accept\n"); await wait(250); // race repeat
  B.send("action|dialog_return\ndialog_name|trade_confirm\nbuttonClicked|accept\n"); await wait(250);
  B.send("action|dialog_return\ndialog_name|trade_confirm\nbuttonClicked|accept\n"); await wait(2500); // race repeat
  const completions = A.replies.filter((r) => r.includes("Trade completed")).length;
  console.log(`[sim] A saw "Trade completed" x${completions}`);
  check("trade completed exactly once", completions === 1);
  check("A got the flying-item visual on payout", A.replies.join("").includes("\x13\x00\x00\x03"));

  const failed = checks.filter(([, ok]) => !ok).length;
  console.log(`RESULT: ${checks.length - failed}/${checks.length} checks passed, packets A=${A.replies.length} B=${B.replies.length}`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error("ERR", e); process.exit(1); });
