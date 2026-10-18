#include "pch.hpp"
#include "automate/trade.hpp"

#include "trade_confirm.hpp"

/* Result of the "Trade Confirmation" dialog (trade::send_confirm_dialog).
   Buttons: "accept" (Do The Trade!) and "back" (Cancel) - retail layout, reference:
   ReU GrowTavern dialog_return.h trade_confirm. */
void trade_confirm(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    const std::string &clicked = hPipe["buttonClicked"];

    if (clicked == "accept")
    {
        printf("[trade] %s pressed Do The Trade!\n", pPeer->growid.c_str());
        trade::confirm_offer(*pPeer);
        return;
    }

    /* "back" (or anything else that is not the confirmation) cancels the trade */
    printf("[trade] %s backed out of the confirmation dialog\n", pPeer->growid.c_str());
    trade::end(*pPeer, "cancel", false);
}
