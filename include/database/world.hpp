#pragma once

enum wstate3 : u_char
{
    S_RIGHT   = 0x00,
    S_LOCKED  = 0x10,
    S_LEFT    = 0x20,
    S_SPLICED = 0x1d, // @todo understand the percise flags.
    S_TOGGLE  = 0x40,
    S_PUBLIC  = 0x80
};

/* locks that only occupy a number of tiles, and not the whole world. */
#define is_tile_lock(id) (id == 202/*small*/ || id == 204/*big*/ || id == 206/*huge*/ || id == 4994/*builder*/)

enum wstate4 : u_char
{
    S_WATER =    0x04,
    S_GLUE =     0x08,
    S_FIRE =     0x10,
    /* paint buckets */
    S_RED =      0x20,
    S_GREEN =    0x40,
    S_YELLOW =   S_RED | S_GREEN,
    S_BLUE =     0x80,
    S_AQUA =     S_GREEN | S_BLUE,
    S_PURPLE =   S_RED | S_BLUE,
    S_CHARCOAL = S_RED | S_GREEN | S_BLUE,
    S_VANISH =   S_RED | S_YELLOW | S_GREEN | S_AQUA | S_BLUE | S_PURPLE | S_CHARCOAL
};

enum lock_state : u_char
{
    DISABLE_MUSIC = 0x10,
    DISABLE_MUSIC_RENDER = 0x20,
    RAINBOWS = 0x80
};

char get_type(const ::item &item);

/* @note range-lock (small/big/huge/builder) flags byte — retail TileExtra_Lock flags.
   Sent verbatim in the lock tile extra so the client's wrench UI shows the right options. */
enum lock_flags : u_char
{
    LOCK_IGNORE_EMPTY   = 0x01, // @note "Ignore empty air" (tile locks only)
    LOCK_DISABLE_MUSIC  = 0x10, // @note world lock parity (see lock_state::)
    LOCK_DISABLE_RENDER = 0x20,
    LOCK_BUILD_ONLY     = 0x40, // @note Builder's Lock: "Only Allow Building!"
    LOCK_LIMIT_ADMINS   = 0x80  // @note Builder's Lock: "Admins Are Limited"
};

/* @note a placed range lock (small/big/huge/builder). Tiles it governs store this
   lock's tile index as their "parent" (2 bytes in block::state[0..1], state[2] bit
   0x02 = the client's HAS_PARENT flag). */
struct tile_lock
{
    tile_lock(::pos _pos, int _owner, u_char _flags = 0) :
        pos(_pos), owner(_owner), flags(_flags) {}

    ::pos pos{};
    int owner{};        // @note peer::user_id
    u_char flags{};     // @note lock_flags:: ("public" lives on the lock block's S_PUBLIC state, like world locks)
    std::vector<int> access{};

    ::blob to_blob() const; // @note u8 flags, i32 owner, i32 count, uids... (client + save shape)
};

struct block 
{
    block(short _fg = 0, short _bg = 0) : 
        fg(_fg), bg(_bg), state(0, 0, 0, 0) {}
    
    short fg{0}, bg{0};
    u_char state[4];

    u_char hits[2] = {0, 0}; // @note fg, bg

    void reset();

    ::blob to_blob() const;
};

/* @note tile "parent" = flat index (y*100+x) of the lock tile governing this tile.
   Stored little-endian in state[0..1]; state[2] bit1 mirrors it as HAS_PARENT. */
inline u_short block_parent(const ::block &b)
{
    return static_cast<u_short>(b.state[0] | (b.state[1] << 8));
}
inline void block_set_parent(::block &b, u_short parent)
{
    b.state[0] = static_cast<u_char>(parent & 0xff);
    b.state[1] = static_cast<u_char>(parent >> 8);
    b.state[2] |= 0x02; // @note HAS_PARENT
}
inline void block_clear_parent(::block &b)
{
    b.state[0] = 0x00;
    b.state[1] = 0x00;
    b.state[2] &= ~0x02;
}
#define cord(x,y) (y * 100 + x)

struct door 
{
    door(std::string _label, std::string _dest, std::string _id, ::pos _pos) : 
        label(_label), dest(_dest), id(_id), pos(_pos) {}

    std::string label{};
    std::string dest{};
    std::string id{};

    ::pos pos{};

    ::blob to_blob() const;
};

struct sign 
{
    sign(std::string _label, ::pos _pos) : 
        label(_label), pos(_pos) {}

    std::string label{};
    u_int idk{0xffffffff}; // @note unsigned bool. 0xffffffff = false and 1 = true.

    ::pos pos{};

    ::blob to_blob() const;
};

struct tree
{
    tree(u_int _tick, u_char _fruit, ::pos _pos) : 
        tick(_tick), fruit(_fruit), pos(_pos) {}

    /* @note MAP data */
    u_int tick{}; // @note last modified in seconds
    u_char fruit{};

    ::pos pos{};

    ::blob to_blob(bool seconds = false) const;
};

struct display
{
    display(u_int _id, ::pos _pos) : 
        id(_id), pos(_pos) {}

    u_int id{};
    ::pos pos{};
};

/* @note provider blocks (ATM, chicken, cow, ...) carry a single u32 on the wire:
   seconds elapsed since the tile was placed. The client compares it against the
   item's `tick` from items.dat to decide "harvest ready".
   @source client jump table 0x15b828c (extra 0x09) reads a u32 at tile+0x5c;
           GTopia TileExtra_Provider::otherData is also u32. */
struct provider
{
    provider(u_int _tick, ::pos _pos) : 
        tick(_tick), pos(_pos) {}

    u_int tick{}; // @note ticks() when the provider was placed / last harvested
    ::pos pos{};
};

struct random_block
{
    random_block(u_char _value, ::pos _pos) : 
        value(_value), pos(_pos) {}

    u_char value{};
    ::pos pos{};
};

struct object 
{
    object(u_short _id = 0, u_short _count = 0, ::pos _pos = {0, 0}, u_int _uid = 0) : 
        id(_id), count(_count), pos(_pos), uid(_uid) {}
    u_short id{};
    u_short count{};
    ::pos pos{};

    u_int uid{};

    ::blob to_blob() const;
};

class world 
{
public:
    world(const std::string &name = "");/*DEFAULT*/
    ~world();

    void save(); // @note persists owner/blocks/objects to MariaDB (~world + periodic autosave call this)

    bool exists(const std::string& name);

    template<typename T>
    void mysql_insert(const std::string& column, const T& value);

    template<typename T>
    void mysql_update(const std::string& column, const T& value);

    template<typename T>
    T    mysql_select(const std::string &column, const std::string &arg = "");
    void mysql_select_all();

    std::string name{};

    int owner{ 00 }; // @note owner of world using peer's user id.
    std::array<int, 20> access{}; // @note {user_id} @credit https://www.growtopiagame.com/forums/member/440629-yeldyt
    bool is_public{}; // @note checks if world is public to break/place
    u_char lock_state{0x00}; // @note uses lock_state::
    u_char minimum_entry_level{1}; // @note minimal level required to enter a world

    u_char visitors{}; // @note the current number of peers in a world, excluding invisable peers
    u_short netid_counter{}; // @note a number that only increases, this value resets during ~world()

    std::vector<::block> blocks; // @note all blocks, size of 1D meaning (6000) instead of 2D (100, 60)
    u_int last_object_uid{0};
    std::vector<::object> objects{};
    std::vector<::door> doors{};
    std::vector<::tile_lock> locks{}; // @note placed range locks (small/big/huge/builder's)
    std::vector<::display> displays{};
    std::vector<::provider> providers{}; // @note provider blocks + their placement tick
    std::vector<::random_block> random_blocks{};
    std::vector<::sign> signs{};
    std::vector<::tree> trees{};

    ::pos spawn{}; // @note position of main door
    ::pos weather{};

    ::blob serialize();
};
extern std::vector<world> worlds;

/* @return comma separated growids on the world's access list ("`4(empty)``" when none). */
extern std::string world_access_list_text(::world &world);

extern void send_action(ENetPeer &p, const std::string &action, const std::string &str);

extern void send_data(ENetPeer &peer, const ::blob &blob);

extern void state_visuals(ENetPeer &peer, gamePacket &&gamePacket);

/* @note PACKET_SEND_LOCK to everyone in the world: announces/marks the tiles a range
   lock currently governs (u16 tile indices as payload; empty for a world lock). */
extern void send_lock_packet(const std::string &world_name, ::pos lock_pos, int owner_uid, int lock_item_id, const std::vector<::pos> &tiles);

extern void tile_apply_damage(ENetEvent &event, gamePacket gamePacket, block &block, u_int value);

/*
* @brief set slot::count to nagative value if you want to remove an amount. 
* @return the remaining amount if exeeds 200. e.g. emplace(slot{0, 201}) returns 1.
*/
extern u_short modify_item_inventory(ENetEvent &event, ::slot slot);

extern void item_change_object(ENetEvent& event, ::gamePacket gamePacket);

extern void merge_object(ENetEvent& event, ::slot slot, const ::pos &pos, ::world &world);
extern void remove_object(ENetEvent& event, signed uid);
extern int  add_object(ENetEvent& event, ::slot slot, const ::pos &pos, ::world &world);

extern void add_drop(ENetEvent &event, ::slot im, ::pos pos, ::world &world);

extern void send_tile_update(ENetEvent &event, gamePacket s, ::block &b, ::world &world);

/*
* @param speed actually just the particle color & visual, not the speed.
* @param id seems to be a 0xc8 multiplier for multiple particles. unsure.
*/
extern void send_particle_effect(ENetEvent &event, const ::pos &pos, ::pos speed, int id = 0xc8*0, float offset = 0.0f);

extern void remove_fire(ENetEvent &event, gamePacket gamePacket, ::block &block, ::world& world);

extern void fireworks(ENetEvent &event, const ::pos &pos);

extern void generate_world(::world &world);

extern bool door_mover(::world &world, const ::pos &pos);

namespace blast
{
    extern void thermonuclear(::world &world);
}
