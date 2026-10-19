#pragma once

namespace lock
{
    /* @return the lock_info stored for a range lock placed at \p pos (nullptr when none). */
    extern ::tile_lock *find(::world &world, const ::pos &pos);

    /* @return the range lock governing the tile at \p pos (walks the tile's parent
       index; nullptr when the tile is unparented / the parent is stale). */
    extern ::tile_lock *governing(::world &world, const ::pos &pos);

    extern bool has_access(const ::tile_lock &lock, int user_id);

    /* @return the max tile count a range lock claims (small 10, big 48, huge/builder 200). */
    extern int max_tiles(short item_id);

    /* Re-claims the tiles a range lock covers (retail "apply lock" algorithm, ported
       from GTopia's WorldTileManager::ApplyLockTiles): repeatedly claims the closest
       still-eligible tile within a growing radius, where eligible = unparented,
       (unless ignore_empty) non-air, not a lock/door/portal, and edge-adjacent to the
       lock or to a tile this lock already owns. Previously owned tiles are released
       first (re-calc). \p out receives every tile now governed by the lock. */
    extern void apply(::world &world, const ::pos &lock_pos, int max, bool ignore_empty, std::vector<::pos> &out);

    /* Releases every tile parented to the lock at \p lock_pos. \p out receives them. */
    extern void clear(::world &world, const ::pos &lock_pos, std::vector<::pos> &out);
}
