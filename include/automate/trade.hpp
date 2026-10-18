#pragma once

/* Trade implementation (P1).
 *
 * Wire formats verified against a real 5.57 client (gtproxy captures + libgrowtopia.so
 * disassembly): OnStartTrade / OnTradeStatus / OnForceTradeEnd varlists, the status data
 * "add_slot|id|count + locked|0|1 + accepted|0|1", and the rule that the first argument
 * (netid) SELECTS the offer panel (own netid -> own panel via arg2 caption, partner netid
 * -> partner panel via arg3 caption).
 *
 * A completed trade moves the offered stacks between the two backpacks (trade::transfer):
 * offers are reservations until both sides accept, then the sender is debited and the
 * receiver credited. */

namespace trade
{
    /* begin a trade between two peers (both clients get OnStartTrade + a status) */
    void start(::peer &a, ::peer &b);

    void add_item(::peer &p, int itemID, int count);
    void remove_item(::peer &p, int itemID);
    void set_accept(::peer &p, bool accepted);
    void set_lock(::peer &p, bool locked);

    /* "Do The Trade!" pressed in the confirmation dialog: flags the side, and once BOTH
       confirmed it re-validates and pays out both offers (with the flying-item visual). */
    void confirm_offer(::peer &p);

    /* play audio/<name> to one peer (no-op if that peer is gone).
       delay_ms: >0 schedules it client-side (delayMS), -1 = immediate. */
    void play_sfx_for(::peer &p, const char *name, int delay_ms = -1);

    /* push the current trade state to this peer (single view) / both sides */
    void send_status(::peer &p);
    void refresh(::peer &p);

    /* kill the trade; the other side always gets OnForceTradeEnd.
       also_self: send it to p as well (used on completion). */
    void end(::peer &p, const char *reason, bool also_self);

    /* main-loop sweep: force-close trades whose completion animation window has passed */
    void sweep();
}
