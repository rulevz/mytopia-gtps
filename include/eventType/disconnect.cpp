#include "pch.hpp"
#include "action/quit.hpp"
#include "disconnect.hpp"

void disconnect(ENetEvent& event)
{
    unsigned char *ip = reinterpret_cast<unsigned char*>(&event.peer->address.host); // @note debug logging
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (g_verbose) printf("[enet] disconnect: %u.%u.%u.%u:%u growid=%s\n", ip[0], ip[1], ip[2], ip[3], event.peer->address.port,
           (pPeer && !pPeer->growid.empty()) ? pPeer->growid.c_str() : "?");

    action::quit(event, "");
}
