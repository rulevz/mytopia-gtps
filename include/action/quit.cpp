#include "pch.hpp"
#include "action/quit_to_exit.hpp"
#include "automate/trade.hpp"
#include "quit.hpp"

void action::quit(ENetEvent& event, const std::string& header) 
{
    /* @note clear any active trade BEFORE the peer object dies: the partner keeps a
       ::peer* and would otherwise point at freed memory (quit_to_exit also ends the
       trade, but it bails early when the peer has no world/netid - belt and braces). */
    if (event.peer != nullptr && event.peer->data != nullptr)
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        if (pPeer->trade.partner != nullptr) trade::end(*pPeer, "disconnected", true);
    }

    action::quit_to_exit(event, "", true);
    
    if (event.peer == nullptr) return;
    if (event.peer->data != nullptr) 
    {
        delete static_cast<::peer*>(event.peer->data);
        event.peer->data = nullptr;
    }
    enet_peer_reset(event.peer);
}