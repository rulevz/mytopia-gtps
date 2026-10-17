#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"

#include "lock_edit.hpp"

void lock_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    if (world->owner != pPeer->user_id) return; // @note only the owner can change the lock

    ::pos pos{};
    pos.x = atoi(hPipe["tilex"].c_str());
    pos.y = atoi(hPipe["tiley"].c_str());
    if (pos.x < 0 || pos.x >= 100 || pos.y < 0 || cord(pos.x, pos.y) >= world->blocks.size()) return;
    if (id_to_item(world->blocks[cord(pos.x, pos.y)].fg).type != type::LOCK) return;
    
    world->is_public = atoi(hPipe["checkbox_public"].c_str());
    if (atoi(hPipe["checkbox_disable_music"].c_str()) != 0)
        world->lock_state |= DISABLE_MUSIC;
    else world->lock_state &= ~DISABLE_MUSIC;
    world->minimum_entry_level = atoi(hPipe["minimum_entry_level"].c_str());

    ::block &block = world->blocks[cord(pos.x, pos.y)];

    if (world->is_public) 
         block.state[2] |= S_PUBLIC;
    else block.state[2] &= ~S_PUBLIC;

    // @note access list: playerNetID (online picker) takes priority, otherwise the
    // name text field is used. A name already listed toggles OFF, a new one ON.
    int target_id = 0;
    std::string target_name{};
    const std::string netid_str = hPipe["playerNetID"];
    if (!netid_str.empty() && atoi(netid_str.c_str()) != 0)
    {
        const short netid = atoi(netid_str.c_str());
        peers(world->name, PEER_SAME_WORLD, [&](ENetPeer &p)
        {
            ::peer *o = static_cast<::peer*>(p.data);
            if (o->netid == netid) { target_id = o->user_id; target_name = o->growid; }
        });
    }
    else if (const std::string name = hPipe["access"]; !name.empty())
    {
        ::peer target{}; // @note offline "fake" peer, only used to read the uid
        target.growid = name;
        target_id = target.mysql_select<signed>("uid");
        target_name = name;
    }

    if (target_id > 0)
    {
        if (auto found = std::ranges::find(world->access, target_id); found != world->access.end())
        {
            *found = 0;
            on::ConsoleMessage(event.peer, std::format("`4{}`` is no longer on the access list.", target_name));
        }
        else if (auto slot = std::ranges::find(world->access, 0); slot != world->access.end())
        {
            *slot = target_id;
            on::ConsoleMessage(event.peer, std::format("`2{}`` has been added to the access list.", target_name));
        }
        else on::ConsoleMessage(event.peer, "`4The access list is full (20 players).``");
    }
    else if (!netid_str.empty() || !hPipe["access"].empty())
        on::ConsoleMessage(event.peer, "`4No player found with that name.``");

    world->save(); // @note persist owner/access immediately

    on::ConsoleMessage(event.peer, std::format("`2{}`` has set the `$World Lock`` to {}``  (access: {})", 
        pPeer->growid, (block.state[2] & S_PUBLIC) ? "`$PUBLIC" : "`4PRIVATE", world_access_list_text(*world)));

    send_tile_update(event, {
        .id = block.fg,
        .punch = pos
    }, block, *world);
}
