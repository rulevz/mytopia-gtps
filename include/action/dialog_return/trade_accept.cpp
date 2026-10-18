#include "pch.hpp"
#include "automate/trade.hpp"

#include "trade_accept.hpp"

/* result of the "Accept this trade?" confirmation (see action::trade_accept).
   The client answers dialog_return -> dialog_name|trade_accept + buttonClicked|<id>. */
void trade_accept(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const std::string &clicked = hPipe["buttonClicked"];

    /* the offer rows are buttons too (icon + count badge) - tapping one must never confirm */
    if (clicked.starts_with("item_"))
    {
        printf("[trade] %s tapped an offer row (%s) - not a confirmation\n", pPeer->growid.c_str(), clicked.c_str());
        return;
    }

    /* any other non-confirmation button means the player backed out */
    if (clicked == "Cancel" || clicked == "No" || clicked == "Close")
    {
        printf("[trade] %s backed out of the accept confirmation\n", pPeer->growid.c_str());
        trade::play_sfx_for(*pPeer, "dialog_cancel.wav");
        return;
    }

    trade::play_sfx_for(*pPeer, "bloop.wav");
    trade::set_accept(*pPeer, true);
}