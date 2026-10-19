#include "pch.hpp"
#include "onVariant/NameChanged.hpp"
#include "onVariant/SetClothing.hpp"
#include "onVariant/Action.hpp"
#include "onVariant/ConsoleMessage.hpp"
#include "commands/weather.hpp"
#include "item_activate.hpp"
#include "tools/random.hpp"
#include "tools/time.hpp"
#include "tools/create_dialog.hpp"
#include "action/quit_to_exit.hpp"
#include "action/join_request.hpp"
#include "item_activate_object.hpp"
#include "automate/lock.hpp"
#include "automate/loot.hpp"

#include "tile_change.hpp"

/* @return growid of any account id (works offline); "someone" when unknown. */
static std::string uid_growid(int user_id)
{
    ::peer offline{}; // @note offline "fake" peer, only used to read the growid
    offline.user_id = user_id;
    const std::string growid = offline.mysql_select<std::string>("growid", "", true);
    return growid.empty() ? "someone" : growid;
}

void tile_change(ENetEvent& event, ::gamePacket gamePacket) 
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    try
    {
        auto world = std::ranges::find(worlds, pPeer->recent_worlds.back(), &::world::name);
        if (world == worlds.end()) return;

        ::block &block = world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)];

        const ::item &item = id_to_item((gamePacket.id != 32 && gamePacket.id != 18) ? gamePacket.id : (block.fg != 0) ? block.fg : block.bg);
        if (item.id == 0) return;

        if (block.state[3] & S_FIRE) // @note allow anyone to take out fire
            if (pPeer->clothing[clothing::HAND] == 3066/* fire hose */)
            {
                remove_fire(event, gamePacket, block, *world);
                return; // @note avoid hitting the block
            }

        if (!(item.cat & CAT_PUBLIC)) // @note if block is public skip validating if peer is owner or access
        {
            const bool is_punch = (gamePacket.id == 18);

            /* @note world lock rule (existing semantics, now with feedback) */
            if ((world->owner && !world->is_public && !pPeer->role) &&
                (pPeer->user_id != world->owner && std::ranges::find(world->access, pPeer->user_id) == world->access.end()))
            {
                if (is_punch)
                    throw std::runtime_error(std::format("`w{}``'s `$World Lock``. (`4No access``)", uid_growid(world->owner)));
                throw std::runtime_error(std::format("That area is owned by `w{}``", uid_growid(world->owner)));
            }

            /* @note range-lock rule: target tile sits inside a small/big/huge/builder's lock */
            if (::tile_lock *range = lock::governing(*world, gamePacket.punch))
            {
                ::block &lock_block = world->blocks[cord(range->pos.x_int(), range->pos.y_int())];
                const ::item &lock_item = id_to_item(lock_block.fg);
                const bool is_public = (lock_block.state[2] & S_PUBLIC) != 0;
                const bool builders = (lock_item.id == 4994);

                bool allowed = lock::has_access(*range, pPeer->user_id) || is_public;
                if (pPeer->role != PLAYER && !(range->flags & LOCK_LIMIT_ADMINS)) allowed = true; // @note staff bypass ("Admins Are Limited" disables it)

                if (!allowed)
                {
                    const bool build_only = (range->flags & LOCK_BUILD_ONLY) != 0;
                    if (builders && build_only && is_punch) throw std::runtime_error("This lock allows building only!");
                    if (builders && !build_only && !is_punch) throw std::runtime_error("This lock allows breaking only!");
                    if (is_punch)
                        throw std::runtime_error(std::format("`w{}``'s `${}``. (`4No access``)", uid_growid(range->owner), lock_item.raw_name));
                    throw std::runtime_error(std::format("That area is owned by `w{}``", uid_growid(range->owner)));
                }
            }
        }

        bool tile_update{};
        bool lock_visuals{}; // @todo this looks sloppy
        bool placed_range{}; // @note true when this call placed a small/big/huge/builder's lock
        std::vector<::pos> claimed_tiles{}; // @note tiles the just-placed range lock governs
        
        if (gamePacket.id == 18) // @note punching a block
        {
            static bool punch{}; // @note true if tile_change has been called within this inital (punch)

            if (!punch) // @note put all multiple punch features here
            {
                punch = true;
                if (pPeer->clothing[clothing::HAND] == 5480) // @note Rayman's Fist
                {
                    ::gamePacket copy_gamePacket = gamePacket;

                    /* @note up and down */
                    if (gamePacket.punch.y == gamePacket.pos.by_32(true).y)
                    {
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                        
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                    /* @note left and right <- -> */
                    else if (gamePacket.punch.x == gamePacket.pos.by_32(true).x)
                    {
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));

                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                    /* @note horizontal adjacent \/ */
                    else if (gamePacket.punch.y != gamePacket.pos.by_32(true).y)
                    {
                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));

                        copy_gamePacket.punch.x += (pPeer->facing_left) ? -1 : 1;
                        copy_gamePacket.punch.y += (gamePacket.punch.y < gamePacket.pos.by_32(true).y) ? -1 : 1;
                        tile_change(event, std::move(copy_gamePacket));
                    }
                }
                punch = false;
            }
            u_char apply_damage_value{}; // @note used to change a tile value without using send_tile_update() 

            if (pPeer->clothing[clothing::HAND] == 2952/*Digger's Spade*/)
            {
                if(item.id == 2/*Dirt*/ || item.id == 14)
                {
                    if (block.fg != 0) block.hits[0] = 3;
                    else block.hits[1] = 3;
    
                    int color = (item.id ==  2/*Dirt*/) ? RandomRange(0x02, 0x03)/* @note idk if this is the correct one, at least by looking at the color it looks like dirt*/ : 
                                  (item.id == 14/*Cave Background*/) ? RandomRange(0x0e, 0x0f) : 0x02;

                    send_particle_effect(event, gamePacket.punch.by_32(), {color, 0x61});
                }
            }
            switch (item.id)
            {
                case 758: // @note Roulette Wheel
                {
                    const u_char number = RandomRange(0, 36);
                    const char color = (number == 0) ? '2' : (RandomRange(0, 3) < 2) ? 'b' : '4';
                    const std::string message = std::format("[{} spun the wheel and got `{}{}``!]", pPeer->display_growid, color, number);
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, message](ENetPeer& peer)
                    {
                        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message }, -1, 2000);
                        on::ConsoleMessage(event.peer, message, 2000);
                    });
                    break;
                }
            }
            switch (item.type)
            {
                case type::STRONG: throw std::runtime_error("It's too strong to break.");
                case type::MAIN_DOOR: throw std::runtime_error("(stand over and punch to use)");
                case type::LOCK:
                {
                    if (is_tile_lock(item.id))
                    {
                        // @note only the lock's owner may smash a range lock; everyone else gets the retail bubble
                        if (::tile_lock *range = lock::find(*world, gamePacket.punch); range && range->owner != pPeer->user_id)
                            throw std::runtime_error(std::format("`w{}``'s `${}``. {}", uid_growid(range->owner), item.raw_name,
                                lock::has_access(*range, pPeer->user_id) ? "(`2Access granted``)" : "(`4No access``)"));
                        break;
                    }

                    if (world->owner != pPeer->user_id)
                    {
                        ::peer owner{}; // @note offline "fake" peer, only used to read the owner's name
                        owner.user_id = world->owner;
                        throw std::runtime_error(std::format("`5[```w{}`` `$World Locked`` by `w{}```5]``", world->name, owner.mysql_select<std::string>("growid", "", true)));
                    }
                    break;
                }
                case type::PROVIDER:
                {
                    //if (ticks() - block.tick >= item.tick)
                    //{
                        switch (item.id)
                        {
                            case 1008: // @note ATM
                            {
                                u_char gems = RandomRange(1, 100); // @note source: https://growtopia.fandom.com/wiki/ATM_Machine
                                for (short i : {100, 50, 10, 5, 1}/* gem type */)
                                    for (; gems >= i; gems -= i/* downgrade type */)
                                        add_drop(event, {112, i}, gamePacket.punch.by_32(), *world);
                                        
                                break;
                            }
                            case 872:/*chicken*/ case 866:/*cow*/ case 1632:/*coffee maker*/ case 3888:/*sheep*/
                            {
                                add_drop(event, ::slot(item.id+2, RandomRange(1, 2)), gamePacket.punch.by_32(), *world);
                                break;
                            }
                            case 5116:/*tea set*/
                            {
                                add_drop(event, ::slot(item.id-2, RandomRange(1, 2)), gamePacket.punch.by_32(), *world);
                                break;
                            }
                            case 2798:/*well*/
                            {
                                add_drop(event, ::slot(822/*water bucket*/, RandomRange(1, 2)), gamePacket.punch.by_32(), *world);
                                break;
                            }
                            case 928:/*science station*/ // @note source: https://growtopia.fandom.com/wiki/Science_Station
                            {
                                short chemcial = 
                                    (!RandomRange(0, 16)) ? chemcial = 918/*P*/ : 
                                    (!RandomRange(0, 8))  ? chemcial = 920/*B*/ : 
                                    (!RandomRange(0, 6))  ? chemcial = 924/*Y*/ : 
                                    (!RandomRange(0, 4))  ? chemcial = 916/*R*/ : chemcial = 914/*G*/;
                                add_drop(event, {chemcial, 1}, gamePacket.punch.by_32(), *world);
                                break;
                            }
                        }
                        //block.tick = ticks();
                        send_tile_update(event, std::move(gamePacket), block, *world); // @note update countdown on provider.

                        pPeer->add_xp(event, 1);
                        return;
                    //}
                    break;
                }
                case type::SEED:
                {
                    auto tree = std::ranges::find(world->trees, gamePacket.punch, &::tree::pos);
                    if (ticks() - tree->tick >= item.tick) // @todo limit this check.
                    {
                        block.hits[0] = 99;
                        add_drop(event, ::slot(item.id - 1, RandomRange(1, tree->fruit*3)), gamePacket.punch.by_32(), *world); // @note fruit (from tree)
                    }
                    break;
                }
                case type::WEATHER_MACHINE:
                {
                    ::block &weather_machine = world->blocks[cord(world->weather.x, world->weather.y)];

                    if (!(block.state[2] & S_TOGGLE) && !(weather_machine.state[2] & S_TOGGLE)) weather_machine.state[2] &= ~S_TOGGLE; // @note so we can avoid the upcoming ^= if the weather machine is already toggled
                    block.state[2] ^= S_TOGGLE; // @note if punched twice it can detoggle that is why we use ^= not |=
                    
                    world->weather = gamePacket.punch;
                    
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [block, item](ENetPeer& p)
                    {
                        send_varlist(&p, { "OnSetCurrentWeather", (block.state[2] & S_TOGGLE) ? get_weather_id(item.id) : 0 });
                    });
                    break;
                }
                case type::TOGGLEABLE_BLOCK:
                case type::TOGGLEABLE_ANIMATED_BLOCK:
                case type::CHEST:
                {
                    block.state[2] ^= S_TOGGLE;
                    break;
                }
                case type::RANDOM:
                {
                    apply_damage_value = 
                        (item.id == 456/*Dice*/) ? RandomRange(0, 5) : 
                        (item.id == 1300/*Roshambo*/) ? RandomRange(1, 3) : 0;

                    auto random = std::ranges::find(world->random_blocks, gamePacket.punch, &::random_block::pos);
                    if (random == world->random_blocks.end())
                    {
                        world->random_blocks.emplace_back(::random_block{apply_damage_value, gamePacket.punch});
                    }
                    else random->value = apply_damage_value;
                    break;
                }
            }
            tile_apply_damage(event, std::move(gamePacket), block, apply_damage_value);

            if (block.hits[0] >= item.hits) block.fg = 0, block.hits[0] = 0;
            else if (block.hits[1] >= item.hits) block.bg = 0, block.hits[1] = 0;
            else return;
            
            tile_update = true; // @note refresh the tile cache after destruction
            block.state[2] &= 0x02; // @note reset tile direction; keep the HAS_PARENT bit so the tile stays inside its lock's area
            block.state[3] &= ~S_VANISH; // @note remove paint
            
            if (item.id == 392/*Heartstone*/ || item.id == 3402/*GBC*/ || item.id == 9350/*Super GBC*/)
            {
                /* @note full weighted drop table (include/automate/loot.cpp):
                   growtopiawiki.com/w/Golden_Booty_Chest. Heartstone and GBC share the
                   pool; the Super GBC adds its 13 exclusives and rolls the Golden Heart
                   Crystal at 4x. */
                const ::slot reward = loot::roll_gbc(item.id);
                add_drop(event, reward, gamePacket.punch.by_32(), *world);

                if (reward.id == 1458)
                {
                    std::string message = std::format("msg|`4The Power of Love! `2{} found a `#Golden Heart Crystal`2 in a `#{}`2!", pPeer->growid, item.raw_name);
                    peers(pPeer->recent_worlds.back(), PEER_ALL, [message](ENetPeer &p)
                    {
                        send_action(p, "log", message.c_str());
                    });
                }
                // @note 1 free Super GBC per 100 smashed GBCs (wiki trivia); GBCs only
                if (item.id == 3402 && ++pPeer->gbc_pity % 100 == 0)
                    modify_item_inventory(event, ::slot{9350, 1});
            }
            else if (item.type == type::LOCK)
            {
                if (is_tile_lock(item.id))
                {
                    // @note range lock smashed: release its area (tile updates) and drop the lock
                    // itself back into the owner's backpack through the CAT_RETURN path below.
                    std::vector<::pos> released;
                    lock::clear(*world, gamePacket.punch, released);
                    std::erase_if(world->locks, [&gamePacket](const ::tile_lock &range) { return range.pos == gamePacket.punch; });
                    printf("[lock] %s smashed %s at (%d,%d) - released %zu tile(s)\n",
                        pPeer->growid.c_str(), item.raw_name.c_str(),
                        gamePacket.punch.x_int(), gamePacket.punch.y_int(), released.size());

                    for (const ::pos &tile : released)
                    {
                        ::block &released_block = world->blocks[cord(tile.x_int(), tile.y_int())];
                        send_tile_update(event, ::gamePacket{.id = released_block.fg, .punch = tile}, released_block, *world);
                    }
                }
                else
                {
                    pPeer->display_growid = std::format("`w{}``", pPeer->growid);
                    on::NameChanged(event);
                    
                    world->owner = 0;
                }
            }

            if (item.cat == CAT_RETURN)
            {
                int uid = add_object(event, ::slot(item.id, 1), gamePacket.pos, *world);
                item_activate_object(event, ::gamePacket{.id = uid, .punch = gamePacket.punch});
            }
            else if (u_char(item.property) & 04) { } // @note "This item never drops any seeds."; should it drop a block?
            else // @note normal break (drop gem, seed, block & give XP)
            {
                if (item.type != type::SEED)
                { /* gem drop */
                    /* if greater than 1, assume it's a farmable.*/
                    u_char rarity_to_gem =
                        (item.rarity >= 87) ? 22 : 
                        (item.rarity >= 68) ? 18 : 
                        (item.rarity >= 53) ? 14 : 
                        (item.rarity >= 41) ? 11 : 
                        (item.rarity >= 36) ? 10 :
                        (item.rarity >= 32) ? 9 :
                        (item.rarity >= 24) ? 5 : 1;

                    if (!RandomRange(0, (rarity_to_gem > 1) ? 1 : 4)) // @note double chances if farmable.
                    {
                        /* @todo merge gems more effectively */
                        u_char gems = RandomRange(1, rarity_to_gem);
                        for (short i : {10, 5, 1}/* gem type */)
                            for (; gems >= i; gems -= i/* downgrade type */)
                                add_drop(event, {112, i}, gamePacket.punch.by_32(), *world);
                    }
                    if (!RandomRange(0, (rarity_to_gem > 1) ? 2 : 4)) add_drop(event, ::slot(item.id + 1, 1), gamePacket.punch.by_32(), *world); 
                    else if (!RandomRange(0, (rarity_to_gem > 1) ? 4 : 8)) add_drop(event, ::slot(item.id, 1), gamePacket.punch.by_32(), *world);
                } /* ~gem drop */

                pPeer->add_xp(event, std::trunc(1.0f + item.rarity / 5.0f));
            }
        } // @note delete im, id
        else if (item.cloth_type != clothing::NONE) 
        {
            if (gamePacket.punch != pPeer->pos.by_32(true)) throw std::runtime_error("To wear clothing, use on yourself");

            item_activate(event, gamePacket);
            return; 
        }
        else if (item.type == type::CONSUMEABLE) 
        {
            if (item.raw_name.find(" Blast") != std::string::npos)
            {
                send_varlist(event.peer, {
                    "OnDialogRequest",
                    std::format(
                        "set_default_color|`o\n"
                        "embed_data|id|{0}\n"
                        "add_label_with_icon|big|`w{1}``|left|{0}|\n"
                        "add_label|small|This item creates a new world! Enter a unique name for it.|left\n"
                        "add_text_input|name|New World Name||24|\n"
                        "end_dialog|create_blast|Cancel|Create!|\n", // @todo rgt "Create!" is a purple-ish pink color
                        item.id, item.raw_name
                    )
                });
            }

            if (item.raw_name.find("Paint Bucket - ") != std::string::npos && pPeer->clothing[clothing::HAND] != 3494) throw std::runtime_error("you need a Paintbrush to apply paint!");
            if (item.raw_name.find("Hair Dye") != std::string::npos)
            {
                if (gamePacket.punch != pPeer->pos.by_32(true)) throw std::runtime_error("Don't spill your dye!");
                else if (world->blocks[cord(pPeer->pos.by_32(true).x, pPeer->pos.by_32(true).y)].fg != 230/*Bathtub*/) throw std::runtime_error("You'll make a huge mess if you do that outside the Bathtub!");

                on::Action(event, "shower");
                // audio/shower.wav
                send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "You dyed your hair!", 0u, 1u });
            }
            float color{}; // @note the color of the particle effect.
            float particle{};
            switch (item.id)
            {
                case 1404: // @note Door Mover
                {
                    if (!door_mover(*world, gamePacket.punch)) throw std::runtime_error("There's no room to put the door there! You need 2 empty spaces vertically.");

                    std::string remember_name = world->name;
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    { 
                        ENetEvent fake{.peer = &p};
                        action::quit_to_exit(fake, "", true); // @note everyone in world exits
                        action::join_request(fake, "", remember_name); // @note everyone in world re-joins
                    });
                    return;
                }
                case 822: // @note Water Bucket
                {
                    if (block.state[3] & S_FIRE) remove_fire(event, gamePacket, block, *world);
                    else block.state[3] ^= S_WATER;
                    break;
                }
                case 1866: // @note Block Glue
                {
                    block.state[3] ^= S_GLUE;
                    break;
                }
                case 3062: // @note Pocket Lighter
                {
                    if (block.fg == 0 && block.bg == 0) throw std::runtime_error("There's nothing to burn!");
                    if (block.state[3] & (S_FIRE | S_WATER)) return; // @note avoid fire on water & fire on fire

                    block.state[3] |= S_FIRE;

                    std::string message = "`7[```4MWAHAHAHA!! FIRE FIRE FIRE```7]``";
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    {
                        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, message, 0u });
                        on::ConsoleMessage(event.peer, message);
                    });
                    particle = 0x96;

                    if (block.fg == 3090) // @note Highly Combustible Box
                    {
                        block.fg = 3128; // @note Combusted Box
                        if (!(block.state[2] & S_TOGGLE)/*closed*/) {} // @todo recipes: https://growtopia.fandom.com/wiki/Guide:Highly_Combustible_Box
                    }
                    break;
                }
                case 3404:/*Sour Lollipop*/ case 3406:/*Sweet Lollipop*/
                {
                    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`#YUM!:D", 0u });

                    break;
                }
                case 3400: // @note Love Potion #8
                {
                    if (block.fg != 10) return; // @note Rock

                    block.fg = 392; // @note Heartstone
                    particle = 0x2c;
                    break;
                }
                case 1488: // @note Experience Potion
                {
                    send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "`#GULP! You got smarter!", 0u });
                    pPeer->add_xp(event, 10000);
                    break;
                }
                case 834: // @note Fireworks
                {
                    fireworks(event, gamePacket.punch.by_32());
                    break;
                }
                case 2480: // @note Megaphone
                {
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        ::create_dialog()
                            .set_default_color("`o")
                            .add_label_with_icon("big", "`wMegaphone``", item.id)
                            .add_textbox("Enter a message you want to broadcast to every player in Growtopia! This will use up 1 Megaphone")
                            .add_text_input("message", "", "", 128)
                            .end_dialog("megaphone", "Nevermind", "Broadcast")
                    });
                    break;
                }
                case 408: // @note Duct Tape
                {
                    peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer& p) 
                    {
                        ::peer *_p = static_cast<::peer*>(p.data);

                        if (gamePacket.punch == _p->pos.by_32(true))
                        {
                            _p->state |= S_DUCT_TAPE;
                            _p->duct_tape_tick = ticks(); // @note expires after 10 minutes (main loop watchdog)
                            on::SetClothing(p);
                        }
                    });
                    break;
                }
                case 3478: // @note Paint Bucket - Red
                {
                    block.state[3] |= S_RED;
                    color = bgra::RED, particle = 0xa8; 
                    break;
                }
                case 3480: // @note Paint Bucket - Yellow
                {
                    block.state[3] |= S_YELLOW;
                    color = bgra::GREEN | bgra::RED, particle = 0xa8;
                    break;
                }
                case 3482: // @note Paint Bucket - Green
                {
                    block.state[3] |= S_GREEN;
                    color = bgra::GREEN, particle = 0xa8;
                    break;
                }
                case 3484: // @note Paint Bucket - Aqua
                {
                    block.state[3] |= S_AQUA;
                    color = bgra::BLUE | bgra::GREEN, particle = 0xa8;
                    break;
                }
                case 3486: // @note Paint Bucket - Blue
                {
                    block.state[3] |= S_BLUE;
                    color = bgra::BLUE, particle = 0xa8;
                    break;
                }
                case 3488: // @note Paint Bucket - Purple
                {
                    block.state[3] |= S_PURPLE;
                    color = bgra::BLUE | bgra::RED, particle = 0xa8; // @note blue + red
                    break;
                }
                case 3490: // @note Paint Bucket - Charcoal
                {
                    block.state[3] |= S_CHARCOAL;
                    color = bgra::BLUE | bgra::GREEN | bgra::RED | bgra::ALPHA, particle = 0xa8; // @note max will provide a pure black color. idk if growtopia is the same.
                    break;
                }
                case 3492: // @note Paint Bucket - Vanish
                {
                    block.state[3] &= ~S_VANISH;
                    color = bgra::BLUE | bgra::GREEN | bgra::RED, particle = 0xa8; // @todo get exact color. I just guessed T-T
                }
                case 3822: break; // Red Hair Dye
                case 3824: break; // Green Hair Dye
                case 3826: break; // Blue Hair Dye
                default: return; // @note prevent taking the consumeable if nothing happended
            }
            if (particle > 0.0f)
            {
                send_particle_effect(event, gamePacket.punch.by_32(), {color, particle});
            }
            send_tile_update(event, std::move(gamePacket), block, *world);

            modify_item_inventory(event, ::slot(item.id, -1));
            pPeer->add_xp(event, 1);
            return;
        }
        else if (gamePacket.id == 32)
        {
            switch (item.type)
            {
                case type::LOCK:
                {
                    if (is_tile_lock(item.id))
                    {
                        ::tile_lock *range = lock::find(*world, gamePacket.punch);
                        if (range == nullptr) break;

                        if (range->owner == pPeer->user_id)
                        {
                            // @note owner: full lock dialog (access list / public / ignore-air / builder options)
                            std::string access_list{ "`4(empty)``" };
                            if (!range->access.empty())
                            {
                                access_list.clear();
                                for (const int uid : range->access)
                                {
                                    if (!access_list.empty()) access_list += ", ";
                                    access_list += uid_growid(uid);
                                }
                            }
                            const bool is_public = (block.state[2] & S_PUBLIC) != 0;

                            std::string dialog = std::format(
                                "set_default_color|`o\n"
                                "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                                "add_popup_name|LockEdit|\n"
                                "add_label|small|`wAccess list:`` {}|left\n"
                                "embed_data|tilex|{}\n"
                                "embed_data|tiley|{}\n"
                                "add_spacer|small|\n"
                                "add_player_picker|playerNetID|`wAdd online player``|\n"
                                "add_text_input|access|Add/remove by name||18|\n"
                                "add_smalltext|Type a name to add them; type a listed name again to remove it.|left|\n"
                                "add_checkbox|checkbox_public|{}|{}\n",
                                item.raw_name, item.id, access_list, gamePacket.punch.x_int(), gamePacket.punch.y_int(),
                                (item.id == 4994) ? "Allow anyone to Build or Break" : "Allow anyone to build",
                                to_char(is_public));
                            if (item.id == 4994)
                            {
                                dialog += std::format(
                                    "add_smalltext|This lock allows Building or Breaking.  (ONLY if \"Allow anyone to Build or Break\" is checked above)!|left|\n"
                                    "add_spacer|small|\n"
                                    "add_smalltext|Leaving this box unchecked only allows Breaking.|left|\n"
                                    "add_checkbox|checkbox_buildonly|Only Allow Building!|{}\n"
                                    "add_smalltext|People with lock access can both build and break unless you check below.  The lock owner can always build and break.|left|\n"
                                    "add_checkbox|checkbox_admins|Admins Are Limited|{}\n",
                                    to_char((range->flags & LOCK_BUILD_ONLY) != 0),
                                    to_char((range->flags & LOCK_LIMIT_ADMINS) != 0));
                            }
                            dialog += std::format(
                                "add_checkbox|checkbox_ignore|Ignore empty air|{}\n"
                                "add_button|recalc_lock|`wRe-apply lock``|noflags|0|0|\n"
                                "end_dialog|tile_lock_edit|Cancel|OK|\n",
                                to_char((range->flags & LOCK_IGNORE_EMPTY) != 0));

                            send_varlist(event.peer, { "OnDialogRequest", dialog });
                        }
                        else if (pPeer->role != PLAYER && lock::has_access(*range, pPeer->user_id))
                        {
                            // @note staff with access: offer to drop it
                            send_varlist(event.peer, {
                                "OnDialogRequest",
                                std::format(
                                    "set_default_color|`o\n"
                                    "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                                    "add_popup_name|LockEdit|\n"
                                    "embed_data|tilex|{}\n"
                                    "embed_data|tiley|{}\n"
                                    "add_label|This lock is owned by someone, but I have access on it.|left\n"
                                    "add_button|remove_my_access|`wRemove My Access``|noflags|0|0|\n"
                                    "end_dialog|tile_lock_edit|Cancel|OK|\n",
                                    item.raw_name, item.id, gamePacket.punch.x_int(), gamePacket.punch.y_int())
                            });
                        }
                        else if (pPeer->role == PLAYER)
                            throw std::runtime_error("I'm `4unable`` to pick the lock.");
                        break;
                    }

                    if (pPeer->user_id == world->owner)
                    {
                        send_varlist(event.peer, {
                            "OnDialogRequest",
                            std::format(
                                "set_default_color|`o\n"
                                "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                                "add_popup_name|LockEdit|\n"
                                "add_label|small|`wAccess list:`` {}|left\n"
                                "embed_data|tilex|{}\n"
                                "embed_data|tiley|{}\n"
                                "add_spacer|small|\n"
                                "add_player_picker|playerNetID|`wAdd online player``|\n"
                                "add_text_input|access|Add/remove by name||18|\n"
                                "add_smalltext|Type a name to add them; type a listed name again to remove it.|left|\n"
                                "add_checkbox|checkbox_public|Allow anyone to Build and Break|{}\n"
                                "add_checkbox|checkbox_disable_music|Disable Custom Music Blocks|{}\n"
                                "add_text_input|tempo|Music BPM|100|3|\n"
                                "add_checkbox|checkbox_disable_music_render|Make Custom Music Blocks invisible|0\n"
                                //"add_smalltext|Your current home world is: JOLEIT|left|\n"           // @todo only show when peer has a set home world.
                                "add_checkbox|checkbox_set_as_home_world|Set as Home World|0|\n"
                                "add_text_input|minimum_entry_level|World Level: |{}|3|\n"
                                "add_smalltext|Set minimum world entry level.|\n"
                                "add_button|sessionlength_dialog|`wSet World Timer``|noflags|0|0|\n"
                                "add_button|changecat|`wCategory: None``|noflags|0|0|\n"
                                "add_button|getKey|Get World Key|noflags|0|0|\n"
                                "end_dialog|lock_edit|Cancel|OK|\n",
                                item.raw_name, item.id, ::world_access_list_text(*world), gamePacket.punch.x, gamePacket.punch.y, to_char(world->is_public), (world->lock_state & DISABLE_MUSIC) ? "1" : "0", world->minimum_entry_level
                            )
                        });
                    }
                    break;
                }
                case type::DOOR:
                case type::PORTAL:
                {
                    std::string label, dest, id{};
                    for (::door& door : world->doors)
                        if (door.pos == gamePacket.punch) { label = door.label, dest = door.dest, id = door.id; break; }
                        
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_text_input|door_name|Label|{}|100|\n"
                            "add_popup_name|DoorEdit|\n"
                            "add_text_input|door_target|Destination|{}|24|\n"
                            "add_smalltext|Enter a Destination in this format: `2WORLDNAME:ID``|left|\n"
                            "add_smalltext|Leave `2WORLDNAME`` blank (:ID) to go to the door with `2ID`` in the `2Current World``.|left|\n"
                            "add_text_input|door_id|ID|{}|11|\n"
                            "add_smalltext|Set a unique `2ID`` to target this door as a Destination from another!|left|\n"
                            "add_checkbox|checkbox_locked|Is open to public|1\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|door_edit|Cancel|OK|", 
                            item.raw_name, item.id, label, dest, id, gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::SIGN:
                {
                    std::string label{};
                    for (::sign& sign : world->signs)
                        if (sign.pos == gamePacket.punch) { label = sign.label; break; }

                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_popup_name|SignEdit|\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_textbox|What would you like to write on this sign?``|left|\n"
                            "add_text_input|sign_text||{}|128|\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|sign_edit|Cancel|OK|", 
                            item.raw_name, item.id, label, gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::ENTRANCE:
                {
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        std::format(
                            "set_default_color|`o\n"
                            "add_label_with_icon|big|`wEdit {}``|left|{}|\n"
                            "add_checkbox|checkbox_public|Is open to public|{}\n"
                            "embed_data|tilex|{}\n"
                            "embed_data|tiley|{}\n"
                            "end_dialog|gateway_edit|Cancel|OK|\n", 
                            item.raw_name, item.id, to_char((block.state[2] & S_PUBLIC)), gamePacket.punch.x, gamePacket.punch.y
                        )
                    });
                    break;
                }
                case type::DISPLAY_BLOCK:
                {
                    // @todo
                    break;
                }
                case type::VENDING_MACHINE:
                {
                    send_varlist(event.peer, {
                        "OnDialogRequest",
                        ::create_dialog()
                            .set_default_color("`o")
                            .add_label_with_icon("big", std::format("`w{}``", item.raw_name), item.id)
                            .add_spacer("small")
                            .embed_data("tilex", gamePacket.punch.x)
                            .embed_data("tiley", gamePacket.punch.y)
                            .add_textbox("This machine is empty.")
                            .add_item_picker("stockitem", "`wPut an item in``", "Choose an item to put in the machine!")
                            .add_smalltext("Upgrade to a DigiVend Machine for `44,000 Gems``.")
                            .add_button("upgradedigital", "Upgrade to DigiVend")
                            .add_spacer("small")
                            .end_dialog("vending", "Close", "")
                    });
                    break;
                }
            }
            return; // @note leave early else wrench will act as a block unlike fist which breaks. this is cause of state_visuals()
        }
        else // @note placing a block
        {
            if (block.fg != 0) // @note placing something ontop of exisitng block
            {
                bool update_tile{};
                switch (items[world->blocks[cord(gamePacket.punch.x, gamePacket.punch.y)].fg].type)
                {
                    case type::DISPLAY_BLOCK:
                    {
                        world->displays.emplace_back(::display(item.id, gamePacket.punch));
                        update_tile = true;
                        break;
                    }
                    case type::SEED:
                    {
                        auto tree = std::ranges::find(world->trees, gamePacket.punch, &::tree::pos);
                        if ((block.state[2] & S_SPLICED) == S_SPLICED) throw std::runtime_error("It would be too dangerous to try to mix three seeds.");
                        for (::item &item : items)
                        {
                            if ((item.splice[0] == gamePacket.id && item.splice[1] == block.fg) ||
                                (item.splice[1] == gamePacket.id && item.splice[0] == block.fg) /* allow reverse splice combo */)
                            {
                                const ::item &splice0 = id_to_item(item.splice[0]);
                                const ::item &splice1 = id_to_item(item.splice[1]);

                                send_varlist(event.peer, {
                                    "OnTalkBubble", 
                                    pPeer->netid, 
                                    std::format("`w{}`` and `w{}`` have been spliced to make a `${} Tree``!", 
                                        splice0.raw_name, splice1.raw_name, item.raw_name.substr(0, item.raw_name.length()-5/* seed*/)), // @todo this is hardcoded
                                    0u, 1u
                                });
                                tree->tick = ticks();
                                block.state[2] = S_SPLICED | (block.state[2] & 0x02); // @todo handle for multiple flags (@note 0x02 kept: HAS_PARENT)

                                block.fg = item.id;
                                update_tile = true;
                                break;
                            }
                        }
                        if ((block.state[2] & S_SPLICED) != S_SPLICED) throw std::runtime_error(std::format("Hmm, it looks like `w{}`` and `w{}`` can't be spliced.", id_to_item(block.fg).raw_name, id_to_item(gamePacket.id).raw_name));
                        break;
                    }
                }
                if (update_tile)
                {
                    modify_item_inventory(event, ::slot(gamePacket.id, -1));
                    send_tile_update(event, std::move(gamePacket), block, *world);
                    return;
                }
            }
            if ((item.type == type::BACKGROUND && block.bg != 0) ||
                (item.type != type::BACKGROUND && block.fg != 0)) return; // @note an extra check, i will later make this cleaner.
            if (item.collision == collision::FULL)
            {
                if (gamePacket.punch == gamePacket.pos.by_32(true)) return; // @todo when moving avoid collision.
            }
            switch (item.type)
            {
                case type::DOOR:
                {
                    world->doors.emplace_back("","","", gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::SIGN:
                {
                    world->signs.emplace_back("", gamePacket.punch);
                    tile_update = true;
                    break;
                }
                case type::LOCK:
                {
                    if (is_tile_lock(item.id))
                    {
                        // @note can't cage another player's lock area (retail message)
                        if (::tile_lock *nearby = lock::governing(*world, gamePacket.punch); nearby && nearby->owner != pPeer->user_id)
                            throw std::runtime_error("Only the world owner can place more locks here!");

                        ::tile_lock &range = world->locks.emplace_back(gamePacket.punch, pPeer->user_id);
                        lock::apply(*world, gamePacket.punch, lock::max_tiles(item.id), (range.flags & LOCK_IGNORE_EMPTY) != 0, claimed_tiles);
                        placed_range = true;
                        tile_update = true; // @note the lock tile itself carries the lock extra
                        printf("[lock] %s placed %s at (%d,%d) - governs %zu tile(s)\n",
                            pPeer->growid.c_str(), item.raw_name.c_str(),
                            gamePacket.punch.x_int(), gamePacket.punch.y_int(), claimed_tiles.size());
                        break;
                    }

                    if (!world->owner)
                    {
                        // @note retail: no world lock while someone else's locks exist
                        if (std::ranges::any_of(world->locks, [pPeer](const ::tile_lock &range) { return range.owner != pPeer->user_id; }))
                            throw std::runtime_error("Your `$World Lock`` can't be placed in this world unless everyone else's locks are removed.");

                        world->owner = pPeer->user_id;
                        lock_visuals = true;

                        pPeer->display_growid = std::format("`2{}``", pPeer->growid);
                        on::NameChanged(event);
                        if (std::ranges::find(pPeer->my_worlds, world->name) == pPeer->my_worlds.end()) 
                        {
                            std::ranges::rotate(pPeer->my_worlds, pPeer->my_worlds.begin() + 1);
                            pPeer->my_worlds.back() = world->name;
                        }
                        std::string placed_message = std::format("`5[```w{}`` has been `$World Locked`` by {}`5]``", world->name, pPeer->growid);
                        peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&event, &pPeer, placed_message](ENetPeer& peer) 
                        {
                            send_varlist(&peer, { "OnTalkBubble", pPeer->netid, placed_message });
                            on::ConsoleMessage(&peer, placed_message);
                        });
                    }
                    else throw std::runtime_error("Only one `$World Lock`` can be placed in a world, you'd have to remove the other one first.");
                    break;
                }
                case type::SEED:
                {
                    world->trees.emplace_back(ticks(), RandomRange(1, 3), gamePacket.punch);
                    block.state[2] = 0x11; // @todo
                    tile_update = true;
                    break;
                }
                case type::ENTRANCE:
                {
                    block.state[2] |= S_PUBLIC;
                    tile_update = true;
                    break;
                }
                case type::PROVIDER:
                {
                    //block.tick = ticks();
                    break;
                }
            }
            block.state[2] |= (pPeer->facing_left) ? S_LEFT : S_RIGHT;
            (item.type == type::BACKGROUND) ? block.bg = gamePacket.id : block.fg = gamePacket.id;
            pPeer->emplace(::slot(item.id, -1));
        }
        gamePacket.netid = pPeer->netid; // @todo sometimes rgt has this as 0
        state_visuals(*event.peer, std::move(gamePacket)); // finished.
        if (tile_update) 
        {
            send_tile_update(event, std::move(gamePacket), block, *world);
        }
        if (lock_visuals) 
        {
            state_visuals(*event.peer, ::gamePacket{
                .type = 0x0f, // @note PACKET_SEND_LOCK
                .netid = world->owner, 
                .state = state::S_EXTENDED, 
                .id = gamePacket.id,
                .punch = gamePacket.punch
            });
        }
        if (placed_range)
        {
            /* @note SEND_LOCK marks every tile the new range lock governs for the whole
               world; the placer gets the retail "Area locked." bubble + lock sound. */
            send_lock_packet(world->name, gamePacket.punch, pPeer->user_id, item.id, claimed_tiles);
            send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, "Area locked.", 0u, 1u });
            send_action(*event.peer, "play_sfx", "file|audio/use_lock2.wav\ndelayMS|-1\n");
        }
    }
    catch (const std::exception& exc)
    {
        send_varlist(event.peer, { "OnTalkBubble", pPeer->netid, exc.what(), 0u, 1u });
        return;
    }
}
