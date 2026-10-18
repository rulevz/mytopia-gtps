#include "pch.hpp"
#include "onVariant/RequestWorldSelectMenu.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "automate/trade.hpp"
#include "quit_to_exit.hpp"

void action::quit_to_exit(ENetEvent& event, const std::string& header, bool skip_selection = false) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    /* @note netid 0 == "not in a world" (join_request assigns a nonzero netid).
       an exit can legitimately arrive twice: tapping the main door fires
       tile_activate (-> this) AND the client also sends its own action|quit_to_exit.
       the second call must not decrement visitors again - that used to underflow the
       counter and delete the world under peers still in it, which reset netids
       (colliding avatars / invisible players). */
    if (pPeer->netid == 0) return;

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return; // @note peer was not in a world, therefore nothing to exit from.

    if (pPeer->trade.partner != nullptr) trade::end(*pPeer, "left world", false); // @note trading requires staying in the same world

    std::string message = std::format("`5<{} left, `w{}`` others here>``", pPeer->display_growid, world->visitors-1);
    std::string netid = std::format("netID|{}\n", pPeer->netid);
    std::string pId = std::format("pId|{}\n", pPeer->user_id); // @note this is found during OnSpawn 'eid', the value is the same for user_id.
    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, pPeer, message, netid, pId](ENetPeer& peer) 
    {
        ::peer *pOthers = static_cast<::peer*>(peer.data);
        if (pOthers->user_id == pPeer->user_id) return;

        on::ConsoleMessage(event.peer, message);
        send_varlist(&peer, { "OnRemove", netid, pId }); // @todo
    });

    if (world->visitors > 0) --world->visitors;
    if (world->visitors <= 0) worlds.erase(world); // @note erase only once truly empty
    pPeer->netid = 0; // this will fix any packets being sent outside of world; this can also be used to check if peer is not in a world.

    pPeer->display_growid = std::format("`w{}``", pPeer->growid);
    if (!skip_selection) on::RequestWorldSelectMenu(event);
}