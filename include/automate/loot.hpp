#pragma once

namespace loot
{
    /* @return a weighted-random drop from the Golden Booty Chest family.
       \p chest_id is the smashed item: 392 Heartstone, 3402 GBC, 9350 Super GBC.
       Data: growtopiawiki.com/w/Golden_Booty_Chest (+ /w/Super_Golden_Booty_Chest);
       the weights are our own rarity tiering - see loot.cpp. */
    extern ::slot roll_gbc(short chest_id);
}
