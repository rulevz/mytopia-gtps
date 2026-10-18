#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "tools/time.hpp"

#include <cstdlib>

#include "trade.hpp"

namespace trade
{
    static ENetPeer *peer_of(::peer &p)
    {
        for (ENetPeer *ep : peers())
            if (ep->data != nullptr && static_cast<::peer*>(ep->data) == &p)
                return ep;
        return nullptr;
    }

    static void notify_deal_changed(::peer &who); // @note defined below (offer-change UX)

    /* @note flood guard: a legitimate client sends one trade op per tap; more than ~12 in a
       second is a scripted flood (packet amplification: every op re-broadcasts statuses to
       both clients). Excess ops are dropped, first hit logs once per window. */
    static bool rate_limited(::peer &p)
    {
        if (ticks() - p.trade.ops_at >= 1u) { p.trade.ops_at = ticks(); p.trade.ops = 0; }
        if (++p.trade.ops > 12u)
        {
            if (p.trade.ops == 13u)
                printf("[trade] %s is flooding trade ops - dropping the rest of this window\n",
                       p.growid.c_str());
            return true;
        }
        return false;
    }

    /* @note sound names come from the server-side reference set (GTopia/GrowServer), so the
       client is guaranteed to have the file; play_sfx is a plain text action. */
    void play_sfx_for(::peer &p, const char *name)
    {
        ENetPeer *ep = peer_of(p);
        if (ep == nullptr) return;
        send_action(*ep, "play_sfx", std::format("file|audio/{}\ndelayMS|-1\n", name));
    }

    void send_status(::peer &p)
    {
        if (p.trade.partner == nullptr) return;

        ENetPeer *ep = peer_of(p);
        if (ep == nullptr) return;

        ::peer &partner = *p.trade.partner;

        /* Modern (5.57) trade status data format, recovered from the client binary
           (libgrowtopia.so parser @0x1773390): newline-separated lines
             add_slot|<itemID>|<count>   (one per slot, ORDER = slot index 0..3)
             locked|<0|1>
             accepted|<0|1>
             reset_locks|1               (optional)
           The client fills ONE panel per packet: an EMPTY 2nd arg targets the
           recipient's own panel, a NON-EMPTY 2nd arg (must carry the partner's
           netid / 3rd arg = partner caption) targets the partner panel.
           (Panel mapping confirmed empirically on the 5.57 client: this side
           swaps what the first draft assumed - the tapped item showed up in the
           partner panel.) */
        auto build = [](const ::peer &who)
        {
            std::string data{};
            for (const ::slot &s : who.trade.items)
                data += std::format("add_slot|{}|{}\n", s.id, s.count);
            data += std::format("locked|{}\n", who.trade.locked ? 1 : 0);
            data += std::format("accepted|{}\n", who.trade.accepted ? 1 : 0);
            return data;
        };

        /* @note temporary marker mode for panel mapping (GUROTOPIA_TRADE_MARKERS=1):
           the FIRST argument (netid) selects the target panel: partner-netid ->
           partner panel (right side, 3rd arg = its caption), own-netid -> own panel
           (2nd arg = its caption). Distinct items + captions so the player can
           report where each channel renders. Remove once pinned. */
        if (std::getenv("GUROTOPIA_TRADE_MARKERS") != nullptr)
        {
            send_varlist(ep, {
                "OnTradeStatus",
                static_cast<signed>(p.netid),           // own netid -> own panel
                "OWN-MARK-1",
                "",
                "add_slot|2|2\nlocked|0\naccepted|0\n"          // dirt x2
            });
            send_varlist(ep, {
                "OnTradeStatus",
                static_cast<signed>(partner.netid),     // partner netid -> partner panel
                " ",
                "PARTNER-MARK-2",
                "add_slot|9640|1\nlocked|0\naccepted|0\n"       // world lock x1
            });
            return;
        }

        /* own panel: netid = the recipient's own -> own panel (2nd arg = its caption) */
        send_varlist(ep, {
            "OnTradeStatus",
            static_cast<signed>(p.netid),
            "Your offer.``",
            "",
            build(p)
        });

        /* partner panel: netid = the partner's -> partner panel (3rd arg = its caption) */
        send_varlist(ep, {
            "OnTradeStatus",
            static_cast<signed>(partner.netid),
            "",
            std::format("{}'s offer.``", partner.growid),
            build(partner)
        });
    }

    void refresh(::peer &p)
    {
        send_status(p);
        if (p.trade.partner != nullptr) send_status(*p.trade.partner);
    }

    static void reset_accepts(::peer &p)
    {
        p.trade.accepted = false;
        if (p.trade.partner != nullptr) p.trade.partner->trade.accepted = false;
    }

    void start(::peer &a, ::peer &b)
    {
        if (&a == &b) return;

        ENetPeer *ea = peer_of(a);
        ENetPeer *eb = peer_of(b);
        if (ea == nullptr || eb == nullptr) return;

        if (a.trade.partner != nullptr || b.trade.partner != nullptr)
        {
            on::ConsoleMessage(ea, "`4You can't trade while one of you is already trading.``");
            return;
        }

        a.trade = {}; b.trade = {};
        a.trade.partner = &b;
        b.trade.partner = &a;

        send_varlist(ea, { "OnStartTrade", b.growid.c_str(), static_cast<signed>(b.netid) });
        send_varlist(eb, { "OnStartTrade", a.growid.c_str(), static_cast<signed>(a.netid) });
        send_status(a);
        send_status(b);
        printf("[trade] start: %s (netid %d) <-> %s (netid %d)\n",
               a.growid.c_str(), a.netid, b.growid.c_str(), b.netid);
    }

    void add_item(::peer &p, int itemID, int count)
    {
        if (p.trade.partner == nullptr) return;
        if (p.trade.close_at != 0) return; // @note deal already done - closing animation window
        if (itemID <= 0) return;
        if (rate_limited(p)) return;

        const ::item &item = id_to_item(itemID);
        if (item.cat & CAT_UNTRADEABLE)
        {
            printf("[trade] %s tried to offer untradeable item %d\n", p.growid.c_str(), itemID);
            play_sfx_for(p, "bleep_fail.wav");
            return;
        }

        if (count <= 0) count = 1;
        if (count > 32767) count = 32767;

        /* an offer is a reservation: it must fit within what the player actually holds */
        int held = 0;
        for (const ::slot &s : p.slots)
            if (s.id == itemID) held += s.count;
        if (count > held) count = held;
        if (count <= 0) return;

        /* @note the client builds exactly 4 offer-slot widgets and its status parser walks
           slotWidgets[i] without a bound (parser @0x1773390) - a 5th stack would run off the
           array client-side. New stacks past the 4th are refused; re-offering an existing
           stack (change the amount) stays allowed. */
        const bool replaces = std::ranges::any_of(p.trade.items,
            [itemID](const ::slot &s) { return s.id == itemID; });
        if (!replaces && p.trade.items.size() >= 4u)
        {
            printf("[trade] %s tried to offer a 5th stack - refused (4 offer slots)\n", p.growid.c_str());
            play_sfx_for(p, "bleep_fail.wav");
            return;
        }

        bool replaced = false;
        for (::slot &s : p.trade.items)
            if (s.id == itemID) { s.count = static_cast<short>(count); replaced = true; break; }
        if (!replaced)
            p.trade.items.emplace_back(static_cast<short>(itemID), static_cast<short>(count));

        /* @note anyone who had accepted is told the deal changed (and loses that accept) */
        if (p.trade.accepted) notify_deal_changed(p);
        if (p.trade.partner->trade.accepted) notify_deal_changed(*p.trade.partner);
        reset_accepts(p);
        p.trade.changed_at = ticks();
        printf("[trade] %s offers item %d x%d (%zu offered)\n",
               p.growid.c_str(), itemID, count, p.trade.items.size());
        refresh(p);
    }

    void remove_item(::peer &p, int itemID)
    {
        if (p.trade.partner == nullptr) return;
        if (p.trade.close_at != 0) return; // @note deal already done
        if (rate_limited(p)) return;

        std::erase_if(p.trade.items, [itemID](const ::slot &s) { return s.id == itemID; });

        if (p.trade.accepted) notify_deal_changed(p);
        if (p.trade.partner->trade.accepted) notify_deal_changed(*p.trade.partner);
        reset_accepts(p);
        p.trade.changed_at = ticks();
        printf("[trade] %s removed item %d\n", p.growid.c_str(), itemID);
        refresh(p);
    }

    /* how many more of itemID the backpack can accept (each entry caps at 200) */
    static int room_for(const ::peer &p, int itemID)
    {
        auto it = std::ranges::find(p.slots, static_cast<short>(itemID), &::slot::id);
        if (it == p.slots.end()) return 200;
        return std::max(0, 200 - static_cast<int>(it->count));
    }

    /* move every offered stack from one backpack to the other.
       Offers are reservations kept in trade.items (they never left the backpack), so the
       sender is debited here - clamped to what is still actually held (the player may have
       spent/dropped items mid-trade) and to what the receiver can hold (200 per entry).
       Both peers get their backpack state pushed by modify_item_inventory. */
    static void transfer(::peer &from, ::peer &to)
    {
        if (&from == &to) return;
        ENetPeer *ep_from = peer_of(from);
        ENetPeer *ep_to = peer_of(to);
        if (ep_from == nullptr || ep_to == nullptr) return;

        ENetEvent ev_from{}; ev_from.peer = ep_from;
        ENetEvent ev_to{};   ev_to.peer   = ep_to;

        for (const ::slot &offer : from.trade.items)
        {
            int held = 0;
            for (const ::slot &s : from.slots)
                if (s.id == offer.id) held += s.count;

            const int move = std::min({ static_cast<int>(offer.count), held, room_for(to, offer.id) });
            if (move <= 0) continue;

            const u_short excess = modify_item_inventory(ev_to, ::slot(offer.id, static_cast<short>(move)));
            modify_item_inventory(ev_from, ::slot(offer.id, static_cast<short>(-move)));

            printf("[trade] transferred %d x%d %s -> %s%s\n",
                   offer.id, move, from.growid.c_str(), to.growid.c_str(),
                   (move < offer.count) ? " (clamped: held/backpack limit)" : "");
            if (excess) on::ConsoleMessage(ep_to, "`4Your backpack is full - some items were left behind.``");
        }
    }

    /* @note retail UX (keichira/GTopia PlayerTrade::SendTradeStatus): anyone who already
       accepted is told the deal changed, with a sound, the moment an offer moves. */
    static void notify_deal_changed(::peer &who)
    {
        ENetPeer *ep = peer_of(who);
        if (ep == nullptr) return;
        send_varlist(ep, { "OnTextOverlay", "The deal has changed." });
        play_sfx_for(who, "tile_removed.wav");
    }

    /* accept-time validation (mirrors keichira/GTopia PlayerTrade::CheckTradingItemsAndGetError):
       every offer must still be backed by the sender's backpack and the receiving side must be
       able to hold it. Entries cap at 200 in our inventory (peer::emplace) - the same
       maxCanHold rule retail applies per item. Empty string == the trade may complete. */
    static std::string check_items(::peer &p, ::peer &receiver)
    {
        constexpr int max_hold = 200;

        for (const ::slot &offer : p.trade.items)
        {
            int held = 0;
            for (const ::slot &s : p.slots)
                if (s.id == offer.id) held += s.count;

            if (offer.count <= 0 || held < offer.count)
                return "`4Oops! Items missing from trade.``";

            int receiver_held = 0;
            for (const ::slot &s : receiver.slots)
                if (s.id == offer.id) receiver_held += s.count;

            const ::item &item = id_to_item(offer.id);
            if (receiver_held + offer.count > max_hold)
                return std::format("{} is carrying too many {} and can't fit that many in their backpack.",
                                   receiver.growid, item.raw_name);

            if (held > max_hold)
                return std::format("{} doesn't have enough free backpack slots available.", p.growid);
        }
        return {};
    }

    void set_accept(::peer &p, bool accepted)
    {
        if (p.trade.partner == nullptr) return;
        if (p.trade.close_at != 0) return; // @note deal already done - input locked out
        if (rate_limited(p)) return;

        ::peer &partner = *p.trade.partner;

        /* @note retail anti-scam: accepting is dead for a moment after an offer change, so a
           fast swap can't ride an accept the other side already gave. */
        if (accepted && !p.trade.accepted && ticks() - p.trade.changed_at < 1u)
        {
            printf("[trade] %s accepted too soon after a change - ignored\n", p.growid.c_str());
            refresh(p);
            return;
        }

        p.trade.accepted = accepted;
        printf("[trade] %s accepted=%d (partner accepted=%d)\n",
               p.growid.c_str(), accepted, partner.trade.accepted);

        if (accepted && partner.trade.accepted)
        {
            /* both sides accepted: validate, then move every offered stack, then force-close */
            std::string reason = check_items(p, partner);
            if (reason.empty()) reason = check_items(partner, p);

            if (!reason.empty())
            {
                ENetPeer *ea = peer_of(p);
                ENetPeer *eb = peer_of(partner);
                if (ea != nullptr) send_varlist(ea, { "OnTextOverlay", reason });
                if (eb != nullptr) send_varlist(eb, { "OnTextOverlay", reason });
                play_sfx_for(p, "bleep_fail.wav");
                play_sfx_for(partner, "bleep_fail.wav");
                p.trade.accepted = false;
                partner.trade.accepted = false; // @note both sides drop the accept on a refusal
                printf("[trade] completion refused: %s\n", reason.c_str());
                refresh(p);
                return;
            }

            transfer(p, partner);      // partner receives what p offered
            transfer(partner, p);      // p receives what partner offered

            ENetPeer *ea = peer_of(p);
            ENetPeer *eb = peer_of(partner);
            if (ea != nullptr) on::ConsoleMessage(ea, "`2Trade completed!``");
            if (eb != nullptr) on::ConsoleMessage(eb, "`2Trade completed!``");
            play_sfx_for(p, "success.wav");
            play_sfx_for(partner, "success.wav");

            /* @note the client animates the item swap when it sees BOTH sides accepted -
               keep the windows alive for a moment so that animation can play, then sweep()
               force-closes them. Input is locked out while close_at is set. */
            p.trade.close_at = ticks() + 3u;
            partner.trade.close_at = p.trade.close_at;
            send_status(p);
            send_status(partner);

            /* @note the offers have been paid out - drop them so no later code path can
               ever pay them a second time (belt-and-braces against dupes) */
            p.trade.items.clear();
            partner.trade.items.clear();
            printf("[trade] completed: items moved, closing windows in ~3s (animation window)\n");
            return;
        }
        refresh(p);
    }

    void set_lock(::peer &p, bool locked)
    {
        if (p.trade.partner == nullptr) return;
        if (p.trade.close_at != 0) return; // @note deal already done
        if (rate_limited(p)) return;

        p.trade.locked = locked;
        printf("[trade] %s locked=%d\n", p.growid.c_str(), locked);
        play_sfx_for(p, "use_lock.wav");
        refresh(p);
    }

    void end(::peer &p, const char *reason, bool also_self)
    {
        if (p.trade.partner == nullptr) { p.trade = {}; return; }

        ::peer &partner = *p.trade.partner;
        ENetPeer *eb = peer_of(partner);
        ENetPeer *ea = peer_of(p);
        printf("[trade] end (%s): %s <-> %s\n", reason, p.growid.c_str(), partner.growid.c_str());

        /* @note the side watching gets told who walked away (retail behaviour) */
        if (std::string_view(reason) == "cancel" && eb != nullptr)
            send_varlist(eb, { "OnTextOverlay", std::format("{} has canceled the trade", p.growid) });
        if (std::string_view(reason) != "completed")
        {
            play_sfx_for(p, "dialog_cancel.wav");
            play_sfx_for(partner, "dialog_cancel.wav");
        }

        if (also_self && ea != nullptr) send_varlist(ea, { "OnForceTradeEnd" });
        if (eb != nullptr) send_varlist(eb, { "OnForceTradeEnd" });

        partner.trade = {};
        p.trade = {};
    }

    /* @note called every main-loop pass: force-close windows whose completion animation
       window has elapsed (the first end() clears both sides; further hits are no-ops). */
    void sweep()
    {
        for (ENetPeer *ep : peers())
        {
            ::peer *p = static_cast<::peer*>(ep->data);
            if (p == nullptr || p->trade.close_at == 0) continue;
            if (ticks() >= p->trade.close_at) end(*p, "completed", true);
        }
    }
}
