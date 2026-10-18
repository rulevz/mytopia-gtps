#include "pch.hpp"
#include "automate/trade.hpp"

#include "trade_accept.hpp"

/* result of the "Accept this trade?" confirmation (see action::trade_accept).
   The client answers dialog_return -> dialog_name|trade_accept + buttonClicked|<id>. */
void trade_accept(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    /* any button that is not a confirmation means the player backed out */
    const std::string &clicked = hPipe["buttonClicked"];
    if (clicked == "Cancel" || clicked == "No" || clicked == "Close")
    {
        printf("[trade] %s backed out of the accept confirmation\n", pPeer->growid.c_str());
        return;
    }

    trade::set_accept(*pPeer, true);
}