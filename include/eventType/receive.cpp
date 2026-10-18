#include "pch.hpp"
#include "action/__action.hpp"
#include "gamePacket/_gamePacket.hpp"
#include "receive.hpp"

void receive(ENetEvent& event) 
{
    std::span<const enet_uint8> data{event.packet->data, event.packet->dataLength};
    switch (data[0ull]) 
    {
        case 2: case 3: 
        {
            std::string header{data.begin() + 4, data.end() - 1};
            unsigned char *ip = reinterpret_cast<unsigned char*>(&event.peer->address.host); // @note debug logging
            if (g_verbose) printf("[enet] text from %u.%u.%u.%u:%u: %.1500s\n", ip[0], ip[1], ip[2], ip[3], event.peer->address.port, header.c_str());
            
            std::ranges::replace(header, '\n', '|');
            const std::vector<std::string> pipes = readch(header, '|');
            if (pipes.size() < 2) break;
            
            std::string action{};
            if (pipes[0ull] == "protocol" || pipes[0ull] == "tankIDName")
            {
                action = pipes[0ull];
            }
            else if (pipes[0ull] == "requestedName") // @note modern session login payload -> reuse tankIDName handler
            {
                action = "tankIDName";
            }
            else action = std::format("{}|{}", pipes[0ull], pipes[1ull]);

            if (const auto i = action_pool.find(action); i != action_pool.end())
                i->second(event, header);
            else if (g_verbose) printf("[enet] no handler for action: %.80s\n", action.c_str()); // @note debug logging
            break;
        }
        case 4: 
        {
            if (event.packet->dataLength < sizeof(::gamePacket)) break;

            ::gamePacket gamePacket = make_gamePacket(event.packet->data);
            gamePacket.size = event.packet->dataLength - sizeof(::gamePacket); // @todo did i do this right? or check for flag ::EXTENDED
            if (g_verbose) printf("[enet] tank: type=0x%02x netid=%d size=%u\n", gamePacket.type, gamePacket.netid, gamePacket.size); // @note debug logging

            // @note guard: reject out-of-bounds tile positions for the packet types that
            // INDEX blocks with punch (tile_change 0x03 / tile_activate 0x07) - garbage
            // values used to index blocks[] unchecked -> crash vector. movement (0x00) and
            // friends legitimately carry punch=(-1,-1) and must pass, or clients silently
            // stop syncing while walking.
            if (gamePacket.type == 0x03 || gamePacket.type == 0x07)
            if (const float px = gamePacket.punch.x, py = gamePacket.punch.y;
                px < 0.0f || px >= 100.0f || py < 0.0f || py >= 60.0f)
            {
                if (g_verbose) printf("[enet] tank: dropped out-of-bounds punch (%.1f,%.1f) type=0x%02x\n", px, py, gamePacket.type);
                break;
            }

            if (const auto i = gamePacket_pool.find(gamePacket.type); i != gamePacket_pool.end())
                i->second(event, std::move(gamePacket));
            break;
        }
        default: printf("received unqiue: %d\n", data[0ull]);
    }
    enet_packet_destroy(event.packet);
}

