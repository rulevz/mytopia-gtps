#include "pch.hpp"
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
