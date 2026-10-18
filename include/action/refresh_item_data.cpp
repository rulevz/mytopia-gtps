#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "refresh_item_data.hpp"

void action::refresh_item_data(ENetEvent& event, const std::string& header) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    printf("[items] %s requested a item-data refresh (serving %zu bytes, hash %u)\n",
           (pPeer != nullptr && !pPeer->growid.empty()) ? pPeer->growid.c_str() : "?", im_data.size(), items_dat_hash);
    on::ConsoleMessage(event.peer, "One moment, updating item data...");
    enet_peer_send(event.peer, 0, enet_packet_create(im_data.data(), im_data.size(), ENET_PACKET_FLAG_RELIABLE));
}