#include "pch.hpp"
#include "tools/create_dialog.hpp"
#include "automate/trade.hpp"

#include "trade_msg.hpp"

static bool as_bool(const std::string &value)
{
    return value == "1" || value == "true";
}

void action::mod_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };

    try
    {
        const int itemID = std::stoi(hPipe["itemID"]);

        const ::item &item = id_to_item(itemID);
        if (item.cat & CAT_UNTRADEABLE)
        {
            send_varlist(event.peer, { "OnTextOverlay", "You can't trade that." });
            return;
        }

        /* a stack needs a quantity pick first - same flow as dropping items */
        int held = 0;
        for (const ::slot &slot : pPeer->slots)
            if (slot.id == itemID) held += slot.count;

        printf("[trade] mod_trade: %s item %d (held %d)\n", pPeer->growid.c_str(), itemID, held);

        if (held > 1)
        {
            send_varlist(event.peer, {
                "OnDialogRequest",
                create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", std::format("`wTrade {}``", item.raw_name), item.id)
                    .add_textbox("How many to trade?")
                    .add_text_input("count", "", static_cast<short>(std::min(held, 200)), 5)
                    .embed_data("itemID", item.id)
                    .end_dialog("trade_item")
            });
            return;
        }

        int count = 1;
        if (!hPipe["count"].empty()) count = std::stoi(hPipe["count"]);
        trade::add_item(*pPeer, itemID, count);
    }
    catch (...) { printf("[trade] mod_trade: unparsable payload (%.120s)\n", header.c_str()); }
}

void action::rem_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };

    try { trade::remove_item(*pPeer, std::stoi(hPipe["itemID"])); }
    catch (...) { printf("[trade] rem_trade: unparsable payload (%.120s)\n", header.c_str()); }
}

void action::trade_accept(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };
    trade::set_accept(*pPeer, as_bool(hPipe["status"]));
}

void action::trade_lock(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };
    trade::set_lock(*pPeer, as_bool(hPipe["status"]));
}

void action::trade_cancel(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    printf("[trade] %s canceled the trade\n", pPeer->growid.c_str());
    trade::end(*pPeer, "cancel", false);
}

void action::trade_started(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (g_verbose) printf("[trade] trade_started from %s (%.100s)\n", pPeer->growid.c_str(), header.c_str());

    /* the client uses this to confirm it opened the window - re-push current state */
    if (pPeer->trade.partner != nullptr) trade::refresh(*pPeer);
}

void action::trade_check_price(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    printf("[trade] %s check_price (%.80s)\n", pPeer->growid.c_str(), header.c_str());
    /* @todo reply once the expected packet shape is known (observed from real client) */
}
