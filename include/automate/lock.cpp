#include "pch.hpp"

#include "lock.hpp"

namespace lock
{
    static bool valid(const ::pos &pos)
    {
        return pos.x_int() >= 0 && pos.x_int() < 100 && pos.y_int() >= 0 && pos.y_int() < 60;
    }

    ::tile_lock *find(::world &world, const ::pos &pos)
    {
        auto it = std::ranges::find(world.locks, pos, &::tile_lock::pos);
        return (it == world.locks.end()) ? nullptr : &*it;
    }

    ::tile_lock *governing(::world &world, const ::pos &pos)
    {
        if (!valid(pos)) return nullptr;

        const ::block &block = world.blocks[cord(pos.x_int(), pos.y_int())];
        const u_short parent = block_parent(block);
        if (parent == 0 || parent >= world.blocks.size()) return nullptr;

        // @note the parent must still be a live range lock (self-heals stale indices)
        if (!is_tile_lock(world.blocks[parent].fg)) return nullptr;

        return find(world, ::pos{parent % 100, parent / 100});
    }

    bool has_access(const ::tile_lock &lock, int user_id)
    {
        return user_id == lock.owner || std::ranges::find(lock.access, user_id) != lock.access.end();
    }

    int max_tiles(short item_id)
    {
        switch (item_id)
        {
            case 202/*Small Lock*/: return 10;
            case 204/*Big Lock*/: return 48;
            case 206/*Huge Lock*/: return 200;
            case 4994/*Builder's Lock*/: return 200;
            default: return 0;
        }
    }

    /* @return true when the tile at (x, y) may be claimed by the lock at lock_index. */
    static bool able_to_lock(const ::world &world, u_short lock_index, const ::pos &lock_pos, int x, int y, bool ignore_empty)
    {
        const ::block &block = world.blocks[cord(x, y)];
        if (block_parent(block) != 0) return false; // @note someone else's tile
        if (ignore_empty && block.fg == 0 && block.bg == 0) return false;

        const ::item &item = id_to_item(block.fg != 0 ? block.fg : block.bg);
        if (item.type == type::LOCK || item.type == type::DOOR || item.type == type::MAIN_DOOR || item.type == type::PORTAL)
            return false;

        /* @note the lock's area must stay connected: only tiles adjacent to the lock
           itself or to a tile this lock already owns are eligible. */
        static const int neighbors[4][2] {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (const auto &offset : neighbors)
        {
            const int nx = x + offset[0], ny = y + offset[1];
            if (nx < 0 || nx >= 100 || ny < 0 || ny >= 60) continue;
            if (block_parent(world.blocks[cord(nx, ny)]) == lock_index) return true;
            if (nx == lock_pos.x_int() && ny == lock_pos.y_int()) return true; // @note adjacent to the lock
        }

        return false;
    }

    void clear(::world &world, const ::pos &lock_pos, std::vector<::pos> &out)
    {
        out.clear();
        const u_short lock_index = static_cast<u_short>(cord(lock_pos.x_int(), lock_pos.y_int()));

        for (u_short i = 0; i < world.blocks.size(); ++i)
        {
            ::block &block = world.blocks[i];
            if (block_parent(block) != lock_index) continue;

            block_clear_parent(block);
            out.emplace_back(i % 100, i / 100);
        }
    }

    void apply(::world &world, const ::pos &lock_pos, int max, bool ignore_empty, std::vector<::pos> &out)
    {
        out.clear();
        if (max <= 0 || !valid(lock_pos)) return;

        const u_short lock_index = static_cast<u_short>(cord(lock_pos.x_int(), lock_pos.y_int()));

        /* @note re-calc: release the tiles this lock owned before re-claiming. */
        for (::block &block : world.blocks)
            if (block_parent(block) == lock_index) block_clear_parent(block);

        int radius = 1;
        int total = 0;
        while (total < max && radius <= 100)
        {
            int best_x = -1, best_y = -1, best_distance = 0x7fffffff;

            for (int x = lock_pos.x_int() - radius; x <= lock_pos.x_int() + radius; ++x)
                for (int y = lock_pos.y_int() - radius; y <= lock_pos.y_int() + radius; ++y)
                {
                    if (x < 0 || x >= 100 || y < 0 || y >= 60) continue;
                    if (x == lock_pos.x_int() && y == lock_pos.y_int()) continue;
                    if (!able_to_lock(world, lock_index, lock_pos, x, y, ignore_empty)) continue;

                    const int distance = std::abs(x - lock_pos.x_int()) + std::abs(y - lock_pos.y_int());
                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_x = x, best_y = y;
                    }
                }

            if (best_x < 0)
            {
                ++radius; // @note nothing in this ring: widen the search
                continue;
            }

            block_set_parent(world.blocks[cord(best_x, best_y)], lock_index);
            out.emplace_back(best_x, best_y);
            ++total;
        }
    }
}
