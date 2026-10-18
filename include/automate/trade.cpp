#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

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
        if (itemID <= 0) return;
        if (count <= 0) count = 1;
        if (count > 32767) count = 32767;

        bool replaced = false;
        for (::slot &s : p.trade.items)
            if (s.id == itemID) { s.count = static_cast<short>(count); replaced = true; break; }
        if (!replaced)
            p.trade.items.emplace_back(static_cast<short>(itemID), static_cast<short>(count));

        reset_accepts(p);
        printf("[trade] %s offers item %d x%d (%zu offered)\n",
               p.growid.c_str(), itemID, count, p.trade.items.size());
        refresh(p);
    }

    void remove_item(::peer &p, int itemID)
    {
        if (p.trade.partner == nullptr) return;

        std::erase_if(p.trade.items, [itemID](const ::slot &s) { return s.id == itemID; });
        reset_accepts(p);
        printf("[trade] %s removed item %d\n", p.growid.c_str(), itemID);
        refresh(p);
    }

    void set_accept(::peer &p, bool accepted)
    {
        if (p.trade.partner == nullptr) return;

        ::peer &partner = *p.trade.partner;
        p.trade.accepted = accepted;
        printf("[trade] %s accepted=%d (partner accepted=%d)\n",
               p.growid.c_str(), accepted, partner.trade.accepted);

        if (accepted && partner.trade.accepted)
        {
            /* v1: no item transfer yet - close + tell both, so the flow is observable */
            ENetPeer *ea = peer_of(p);
            if (ea != nullptr) on::ConsoleMessage(ea, "`2Trade completed!`` (capture build - items not transferred yet)");
            ENetPeer *eb = peer_of(partner);
            if (eb != nullptr) on::ConsoleMessage(eb, "`2Trade completed!`` (capture build - items not transferred yet)");
            end(p, "completed", true);
            return;
        }
        refresh(p);
    }

    void set_lock(::peer &p, bool locked)
    {
        if (p.trade.partner == nullptr) return;

        p.trade.locked = locked;
        printf("[trade] %s locked=%d\n", p.growid.c_str(), locked);
        refresh(p);
    }

    void end(::peer &p, const char *reason, bool also_self)
    {
        if (p.trade.partner == nullptr) { p.trade = {}; return; }

        ::peer &partner = *p.trade.partner;
        ENetPeer *eb = peer_of(partner);
        ENetPeer *ea = peer_of(p);
        printf("[trade] end (%s): %s <-> %s\n", reason, p.growid.c_str(), partner.growid.c_str());

        if (also_self && ea != nullptr) send_varlist(ea, { "OnForceTradeEnd" });
        if (eb != nullptr) send_varlist(eb, { "OnForceTradeEnd" });

        partner.trade = {};
        p.trade = {};
    }
}
