#include "pch.hpp"
#include "tools/random.hpp"

#include "loot.hpp"

/* Golden Booty Chest family drop table (392 Heartstone / 3402 GBC / 9350 Super GBC).

   Data: growtopiawiki.com/w/Golden_Booty_Chest - item list, stack counts and the rules
   (Heartstone "uses the same loot table"; Super GBC "mostly shares the same prize pool"
   plus 13 exclusives and is 4x more likely to give a Golden Heart Crystal).

   Retail weights are server-side and not public, so the weights below are our own
   rarity tiering: jackpot ~1/260 per smash, rare 12, uncommon 30, common 90 per entry. */
namespace loot
{
    struct entry
    {
        short id;
        int weight;
        short count;
    };

    static constexpr entry gbc_pool[] =
    {
        // @note jackpot (~1/260 per smash; rolled at 4x on the Super GBC)
        {1458, 10, 1}, // Golden Heart Crystal

        // @note rare tier
        {362, 12, 1}, // Angel Wings
        {366, 12, 1}, // Heartbow
        {1470, 12, 1}, // Ruby Necklace
        {2384, 12, 1}, // Love Bug
        {2388, 12, 1}, // Heartbreaker Hammer
        {3396, 12, 1}, // Lovebird Pendant
        {4412, 12, 1}, // Pegasus
        {5648, 12, 1}, // USMom's Talaria
        {5658, 12, 1}, // Love Birds
        {7818, 12, 1}, // Heartking's Crown
        {7820, 12, 1}, // Heartking's Robes
        {7822, 12, 1}, // Heartqueen's Crown
        {7824, 12, 1}, // Heartqueen's Robes
        {7826, 12, 1}, // Heartstaff
        {7830, 12, 1}, // Heartsword
        {9324, 12, 1}, // Heartthrob Helm
        {10654, 12, 1}, // Love Portal
        {13144, 12, 1}, // Rink of Love
        {14230, 12, 1}, // Bridal Dress with Train
        {14236, 12, 1}, // Harp of Love
        {15192, 12, 1}, // Good Witch's Wand
        {15196, 12, 1}, // Rose Cape

        // @note uncommon tier
        {2390, 30, 1}, // Teeny Angel Wings
        {2396, 30, 1}, // Valensign
        {3398, 30, 1}, // Booty Chest
        {4416, 30, 1}, // Heart Glasses
        {4424, 30, 1}, // Valentine Dress
        {5644, 30, 1}, // Heart Shirt
        {5652, 30, 1}, // Heart Necklace
        {5656, 30, 1}, // Teddy Bear Block
        {9332, 30, 1}, // Dating Dandy's Suit
        {9334, 30, 1}, // Dating Dandy's Hair
        {9336, 30, 1}, // Dating Vixen's Dress
        {9338, 30, 1}, // Dating Vixen's Hair
        {9340, 30, 1}, // Datemaster's Rose
        {9342, 30, 1}, // Datemaster's Bling
        {10626, 30, 1}, // Rose Rifle
        {10632, 30, 1}, // Silk Scarf
        {10652, 30, 1}, // Love Charged Hands
        {10662, 30, 1}, // Tattooed Muscles
        {11682, 30, 1}, // Formal Rose Jacket
        {11684, 30, 1}, // Formal Rose Skirt
        {11686, 30, 1}, // Formal Rose Pants
        {11692, 30, 1}, // Dancing Candle
        {13118, 30, 1}, // Bear That Cares
        {13130, 30, 1}, // Strawberry Hair
        {13132, 30, 1}, // Strawberry Dress
        {13150, 30, 1}, // Cotton Candy Swirl
        {13152, 30, 1}, // Cotton Candy Swirl Background
        {14242, 30, 1}, // Groom's Coat
        {14244, 30, 1}, // Half Up Hairstyle
        {14252, 30, 1}, // Romantic Pic-Nic Basket

        // @note common tier (bulk blocks + event basics)
        {364, 90, 1}, // Diaper
        {384, 90, 1}, // Valentine
        {386, 90, 1}, // Candy Heart
        {388, 90, 1}, // Perfume
        {390, 90, 10}, // Heartstone Block
        {3400, 90, 1}, // Love Potion #8
        {3404, 90, 10}, // Sour Lollipop
        {3406, 90, 1}, // Sweet Lollipop
        {3408, 90, 10}, // Pink Marble Arch
        {4400, 90, 10}, // Heart Awning
        {4418, 90, 2}, // Loveseat
        {4422, 90, 1}, // Chocolate Heart
        {5660, 90, 2}, // Love Curtain
        {5664, 90, 1}, // Valentine's Card
        {7808, 90, 10}, // Heartcastle Stone
        {7810, 90, 10}, // Heartcastle Stone Background
    };

    /* @note items that ONLY the Super GBC can drop (wiki: /w/Super_Golden_Booty_Chest). */
    static constexpr entry super_pool[] =
    {
        {15188, 12, 1}, // Angelic Circlet
        {13126, 12, 1}, // Crimson Thief Coat
        {13124, 12, 1}, // Crimson Thief Hair
        {13116, 12, 1}, // Crimson Thief Mask
        {13128, 12, 1}, // Crimson Thief Pants
        {11668, 12, 1}, // Heart Balloons
        {11664, 12, 1}, // Heartbreak Wings
        {11680, 12, 1}, // Formal Rose Necktie
        {11678, 12, 1}, // Formal Rose Shoulders
        {14238, 12, 1}, // Lace Parasol
        {14232, 12, 1}, // Mobile Suit Wings
        {15198, 12, 1}, // Sparkling Mallet
        {13114, 12, 1}, // Stained Glass Scepter
    };

    ::slot roll_gbc(short chest_id)
    {
        const bool is_super = (chest_id == 9350);

        auto weight_of = [is_super](const entry &e)
        {
            // @note Super GBC: 4x the Golden Heart Crystal chance (wiki)
            return (is_super && e.id == 1458) ? e.weight * 4 : e.weight;
        };

        int total{};
        for (const entry &e : gbc_pool) total += weight_of(e);
        if (is_super)
            for (const entry &e : super_pool) total += e.weight;

        int roll = RandomRange(0, total); // @note uniform in [0, total)

        for (const entry &e : gbc_pool)
        {
            roll -= weight_of(e);
            if (roll < 0) return ::slot(e.id, e.count);
        }
        if (is_super)
            for (const entry &e : super_pool)
            {
                roll -= e.weight;
                if (roll < 0) return ::slot(e.id, e.count);
            }

        return ::slot(388/*Perfume*/, 1); // @note unreachable (roll < total)
    }
}
