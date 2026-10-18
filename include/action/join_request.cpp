#include "pch.hpp"
#include "onVariant/EmoticonDataChanged.hpp"
#include "onVariant/Spawn.hpp"
#include "onVariant/BillboardChange.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/CountryState.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/weather.hpp"
#include "tools/time.hpp"

#include "join_request.hpp"

void action::join_request(ENetEvent& event, const std::string& header, const std::string_view world_name = "") 
{
    try 
    {
        ::peer *pPeer = static_cast<::peer*>(event.peer->data);
        ::hPipe hPipe{ header };

        std::string name =  hPipe["name"];
        if (name.empty() && !world_name.empty()) name = world_name;

        if (name.length() > 24) throw std::runtime_error(""); // @note impossible unless using a proxy since client caps at 24
        if (!alnum(name)) throw std::runtime_error("Sorry, spaces and special characters are not allowed in world or door names.  Try again.");

        for (char &c : name) c = std::toupper(c); // @note start -> START
        
        auto it = std::ranges::find(worlds, name, &::world::name);
        if (it == worlds.end()) 
            it = worlds.emplace(worlds.end(), name);
            
        ::world &world = *it;
        if (world.owner != 0 && !world.is_public && pPeer->user_id != world.owner &&
            std::ranges::find(world.access, pPeer->user_id) == world.access.end() && pPeer->role == PLAYER)
            throw std::runtime_error(std::format("`4{}`` is a locked world. Only the owner and players on the access list can enter!", world.name));
        {
            ::blob blob = compress_state(::gamePacket{ .type = 0x04, /*PACKET_SEND_MAP_DATA*/ .state = state::S_EXTENDED });
            blob.push_back(world.serialize());

            ENetPacket *packet = enet_packet_create(blob.data().data(), blob.size(), ENET_PACKET_FLAG_RELIABLE);
            if (enet_peer_send(event.peer, 0, packet)) enet_packet_destroy(packet);
        } // @note delete blob
        {
            std::string *this_world = std::ranges::find(pPeer->recent_worlds, world.name);
            std::string *end = pPeer->recent_worlds.end();
            std::string *first = this_world != end ? this_world : pPeer->recent_worlds.begin();

            std::rotate(first, first + 1, end);
            pPeer->recent_worlds.back() = world.name;
        } // @note delete name, first
        on::EmoticonDataChanged(event);

        if (pPeer->user_id == world.owner) 
            pPeer->display_growid = std::format("`2{}``", pPeer->growid);
        else if (std::ranges::find(world.access, pPeer->user_id) != world.access.end()) 
            pPeer->display_growid = std::format("`c{}``", pPeer->growid);

        pPeer->rest_pos = world.spawn;
        pPeer->pos = world.spawn; // @note baseline world position: a peer that never sends
                                  // movement (idle clients) must still spawn visibly at the
                                  // door tile for everyone else, not at (0,0)
        if (pPeer->display_growid.empty()) // @note clients that skip enter_game (sims/bots)
            pPeer->display_growid = std::format("`w{}``", pPeer->growid);

        /* @note assign a netid that no peer in this world claims - a recreated world
           object (fresh counter) must never collide with peers lingering from an
           older one (netid collisions break client-side avatars / make players invisible). */
        for (;;)
        {
            pPeer->netid = ++world.netid_counter;
            const bool taken = std::ranges::any_of(peers(), [&](ENetPeer *other)
            {
                ::peer *pOthers = static_cast<::peer*>(other->data);
                return pOthers != nullptr && pOthers != pPeer && pOthers->netid == pPeer->netid &&
                       pOthers->recent_worlds.back() == world.name;
            });
            if (!taken) break;
        }
        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, &world](ENetPeer& peer/*send to everyone in world*/) 
        {
            ::peer *pOthers = static_cast<::peer*>(peer.data); // @note everyone in world's peer.data
            
            if (pOthers->user_id != pPeer->user_id)
            {
                on::Spawn(*event.peer, pOthers->netid, pOthers->user_id, pOthers->pos, pOthers->display_growid, pOthers->country, pOthers->role, pOthers->role >= DEVELOPER, false);
                on::Spawn(peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, false);
                on::SetClothing(peer);
                on::ConsoleMessage(&peer, std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors));
            }
            

            if (pOthers->user_id != pPeer->user_id) // @note the reason this is here is cause we need the peer's OnSpawn to happen before OnTalkBubble
            {
                send_varlist(&peer, {
                    "OnTalkBubble",
                    pPeer->netid,
                    std::format("`5<{} entered, `w{}`` others here>``", pPeer->display_growid, world.visitors),
                    1u
                });
            }
        });
        on::Spawn(*event.peer, pPeer->netid, pPeer->user_id, pPeer->rest_pos, pPeer->display_growid, pPeer->country, pPeer->role, pPeer->role >= DEVELOPER, true);

        if (pPeer->billboard.id != 0) on::BillboardChange(event); // @note don't waste memory if billboard is empty.

        send_varlist(event.peer, {
            "OnSetPos", 
            CL_Vec2f{pPeer->rest_pos.x, pPeer->rest_pos.y}
        }, pPeer->netid);

        on::ConsoleMessage(event.peer, 
            std::format(
                "World `w{}`` entered.  There are `w{}`` other people here, `w{}`` online.", 
                world.name, world.visitors, peers().size()
            )
        );
        ++world.visitors;
        on::SetClothing(*event.peer);
        on::CountryState(event);
    }
    catch (const std::exception& exc)
    {
        send_varlist(event.peer, { "OnFailedToEnterWorld" });
        if (const std::string_view msg{ exc.what() }; !msg.empty()) on::ConsoleMessage(event.peer, std::string{ msg }); // @note tell the player why
        return;
    }
}