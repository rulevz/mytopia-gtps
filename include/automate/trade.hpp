#pragma once

/* Capture-first trade implementation (P1).
 *
 * Wire formats follow the community reference (keichira/GTopia): OnStartTrade /
 * OnTradeStatus / OnForceTradeEnd varlists + the "slot|i|id|count / accepted|0|1 /
 * locked|0|1" status text. These still need validation against a real 5.57 client -
 * that is exactly what the gtproxy capture session is for (tools/gtproxy/README.md).
 *
 * v1 scope: the client-side flow works (window, offer, lock, accept, cancel) and
 * every client message is logged for captures, but a completed trade does NOT
 * transfer items yet (see [trade] logs). */

namespace trade
{
    /* begin a trade between two peers (both clients get OnStartTrade + a status) */
    void start(::peer &a, ::peer &b);

    void add_item(::peer &p, int itemID, int count);
    void remove_item(::peer &p, int itemID);
    void set_accept(::peer &p, bool accepted);
    void set_lock(::peer &p, bool locked);

    /* push the current trade state to this peer (single view) / both sides */
    void send_status(::peer &p);
    void refresh(::peer &p);

    /* kill the trade; the other side always gets OnForceTradeEnd.
       also_self: send it to p as well (used on completion). */
    void end(::peer &p, const char *reason, bool also_self);
}
