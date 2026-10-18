#include "pch.hpp"
#include "automate/trade.hpp"

#include "trade_item.hpp"

/* "How many to trade?" dialog result (same flow as the drop quantity dialog).
   The client submits: dialog_name|trade_item + count|N + itemID|X */
void trade_item(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    try
    {
        const int itemID = std::stoi(hPipe["itemID"]);
        int count = std::stoi(hPipe["count"]);

        int held = 0;
        for (const ::slot &slot : pPeer->slots)
            if (slot.id == itemID) held += slot.count;

        if (count < 1) count = 1;
        if (held > 0 && count > held) count = held; // @note clamp like the drop flow

        trade::add_item(*pPeer, itemID, count);
    }
    catch (...) { printf("[trade] trade_item: unparsable dialog payload\n"); }
}
