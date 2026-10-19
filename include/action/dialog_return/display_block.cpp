#include "pch.hpp"

#include "display_block.hpp"

/* @note Display Block wrench dialog return.
   @source rules: growtopiawiki.com/w/Display_Block
     - only the block's owner may add or remove a displayed item
     - locks, Screen Doors and untradeable items cannot be displayed
     - a freshly placed block needs a 5 second warm-up */
void display_block(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    const short tilex = atoi(hPipe["tilex"].c_str());
    const short tiley = atoi(hPipe["tiley"].c_str());

    auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
    if (world == worlds.end()) return;

    if (tilex < 0 || tilex >= 100 || tiley < 0 || tiley >= 60 || cord(tilex, tiley) >= world->blocks.size()) return;

    const ::pos pos{tilex, tiley};
    ::block &block = world->blocks[cord(tilex, tiley)];
    if (id_to_item(block.fg).type != type::DISPLAY_BLOCK) return;

    // @note owner-only; an UNOWNED world (owner 0) has no restriction, same rule the
    //       build path uses (gateway_edit etc.). Staff do NOT bypass this one.
    if (world->owner != 0 && world->owner != pPeer->user_id)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "Only the block's owner can place items in it." });
        return;
    }

    auto it = std::ranges::find(world->displays, pos, &::display::pos);

    if (hPipe["dialog_name"] == "display_block" && !hPipe["removedisplay"].empty())
    {
        if (it == world->displays.end()) return;
        world->displays.erase(it);
    }
    else
    {
        const int item_id = atoi(hPipe["displayitem"].c_str());
        if (item_id <= 0) return;

        const ::item &item = id_to_item(item_id);

        // @note retail refusals
        if (item.type == type::LOCK || item.type == type::DOOR || item.type == type::MAIN_DOOR)
        {
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "Sorry, no displaying Display Blocks or Locks" });
            return;
        }
        if (std::ranges::find(pPeer->slots, item_id, &::slot::id) == pPeer->slots.end())
            return; // @note not held (client picker is authoritative, but never trust it)

        if (it != world->displays.end()) it->id = item_id;
        else world->displays.emplace_back(item_id, pos);

        pPeer->emplace(::slot(item.id, -1)); // @note the displayed item leaves the backpack
    }

    send_tile_update(event, {
        .id = block.fg,
        .punch = { tilex, tiley }
    }, block, *world);
}
