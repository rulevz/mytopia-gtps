#include "pch.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "automate/lock.hpp"

#include "tile_lock_edit.hpp"

/* Range-lock (small/big/huge/builder's) wrench dialog: access list, public flag,
   "Ignore empty air" + Re-apply lock, Builder's Lock build/break options.
   Dialog name "tile_lock_edit" (see the LOCK wrench branch in tile_change.cpp). */
void tile_lock_edit(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    ::pos pos{};
    pos.x = atoi(hPipe["tilex"].c_str());
    pos.y = atoi(hPipe["tiley"].c_str());
    if (pos.x < 0 || pos.x >= 100 || pos.y < 0 || pos.y >= 60 || cord(pos.x, pos.y) >= world->blocks.size()) return;

    ::block &lock_block = world->blocks[cord(pos.x, pos.y)];
    if (!is_tile_lock(lock_block.fg)) return;

    ::tile_lock *range = lock::find(*world, pos);
    if (range == nullptr) return;

    const std::string clicked = hPipe["buttonClicked"];
    if (clicked == "Cancel") return;

    /* @note staff with access: "Remove My Access" */
    if (clicked == "remove_my_access")
    {
        if (range->owner == pPeer->user_id || !lock::has_access(*range, pPeer->user_id)) return;

        std::erase(range->access, pPeer->user_id);
        send_tile_update(event, ::gamePacket{.id = lock_block.fg, .punch = pos}, lock_block, *world);
        on::ConsoleMessage(event.peer, std::format("`4You removed your access from the {}``.", id_to_item(lock_block.fg).raw_name));
        world->save();
        return;
    }

    if (range->owner != pPeer->user_id) return; // @note everything below is owner-only

    bool tile_needs_update{};

    /* @note checkboxes: missing key = not part of this return (e.g. a pure button click), keep as-is */
    auto apply_checkbox = [&hPipe, &range, &tile_needs_update](const char *key, u_char bit)
    {
        const std::string value = hPipe[key];
        if (value.empty()) return;
        const bool on = atoi(value.c_str()) != 0;
        if (on == ((range->flags & bit) != 0)) return;
        if (on) range->flags |= bit;
        else range->flags &= ~bit;
        tile_needs_update = true;
    };
    apply_checkbox("checkbox_ignore", LOCK_IGNORE_EMPTY);
    apply_checkbox("checkbox_buildonly", LOCK_BUILD_ONLY);
    apply_checkbox("checkbox_admins", LOCK_LIMIT_ADMINS);

    /* @note "public" lives on the lock block (same as the world lock's S_PUBLIC) */
    if (const std::string value = hPipe["checkbox_public"]; !value.empty())
    {
        const bool was = (lock_block.state[2] & S_PUBLIC) != 0;
        if (atoi(value.c_str()) != 0) lock_block.state[2] |= S_PUBLIC;
        else lock_block.state[2] &= ~S_PUBLIC;
        if (was != ((lock_block.state[2] & S_PUBLIC) != 0)) tile_needs_update = true;
    }

    /* @note access list: playerNetID (online picker) first, then the name text field.
       A listed name toggles OFF, a new one ON (same UX as the world lock dialog). */
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
        if (target_id == range->owner)
            on::ConsoleMessage(event.peer, "`4That player already owns this lock.``");
        else if (auto found = std::ranges::find(range->access, target_id); found != range->access.end())
        {
            range->access.erase(found);
            tile_needs_update = true;

            // @note tell the removed player (any world) - matches the retail flow
            peers("", PEER_ALL, [&](ENetPeer &p)
            {
                ::peer *o = static_cast<::peer*>(p.data);
                if (o->user_id != target_id) return;
                send_varlist(&p, { "OnTalkBubble", pPeer->netid, std::format("`w{}`` has `4removed`` your access to a lock on world `w{}``.", pPeer->growid, world->name), 0u, 1u });
                send_action(p, "play_sfx", "file|audio/dialog_cancel.wav\ndelayMS|-1\n");
            });
            on::ConsoleMessage(event.peer, std::format("`4{}`` is no longer on the lock's access list.", target_name));
        }
        else if (range->access.size() >= 20)
            on::ConsoleMessage(event.peer, "`4The lock's access list is full (20 players).``");
        else
        {
            range->access.push_back(target_id);
            tile_needs_update = true;
            on::ConsoleMessage(event.peer, std::format("`2{}`` has been added to the lock's access list.", target_name));
        }
    }
    else if (!netid_str.empty() || !hPipe["access"].empty())
        on::ConsoleMessage(event.peer, "`4No player found with that name.``");

    /* @note "Re-apply lock" button: re-run the claim algorithm with the current setting */
    if (clicked == "recalc_lock")
    {
        std::vector<::pos> claimed;
        lock::apply(*world, pos, lock::max_tiles(lock_block.fg), (range->flags & LOCK_IGNORE_EMPTY) != 0, claimed);
        send_lock_packet(world->name, pos, range->owner, lock_block.fg, claimed);
        send_action(*event.peer, "play_sfx", "file|audio/use_lock2.wav\ndelayMS|-1\n");
        printf("[lock] %s re-applied %s at (%d,%d) - governs %zu tile(s)\n",
            pPeer->growid.c_str(), id_to_item(lock_block.fg).raw_name.c_str(), pos.x_int(), pos.y_int(), claimed.size());
    }

    if (tile_needs_update)
        send_tile_update(event, ::gamePacket{.id = lock_block.fg, .punch = pos}, lock_block, *world);

    world->save();

    if (clicked.empty() || clicked == "OK")
        on::ConsoleMessage(event.peer, std::format("`2{}`` saved.  (access: {})", id_to_item(lock_block.fg).raw_name, range->access.size()));
}
