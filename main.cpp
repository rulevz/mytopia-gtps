/*
    @copyright gurotopia (c) 2024-05-25
    @version parent SHA: 8f2b5977fd057b99e2e2a6f0e65170145decc159 2026-9-30
*/
#include "include/pch.hpp"
#include "include/eventType/_eventType.hpp"

#include "include/database/shouhin.hpp" // @note init_shouhin_tachi()
#include "include/https/https.hpp" // @note https::listener()
#include "include/https/server_data.hpp" // @note gServer_data
#include "include/database/database_config.hpp" // @note load_database_config(), gDatabase_config
#include "include/automate/holiday.hpp" // @note holiday
#include "include/automate/chat_filter.hpp" // @note parse_chat_filter()
#include "include/onVariant/SetClothing.hpp" // @note on::SetClothing (duct tape expiry)
#include "include/tools/time.hpp" // @note ticks()
#include <csignal>
#include <chrono>

bool g_verbose = false;

namespace
{
    volatile std::sig_atomic_t gSignal = 0;
}
static void signal_handler(int signal) { gSignal = signal; }

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg{ argv[i] };
        if (arg == "--verbose" || arg == "-v") g_verbose = true;
    }
    std::signal(SIGINT, signal_handler);
#ifdef SIGHUP // @note unix
    std::signal(SIGHUP, signal_handler); // @note PuTTY, SSH problems
#endif

    mysql_library_init(0, NULL, NULL);
    enet_initialize();
    {
        gServer_data.init(); // @note ./server_data.php
        ENetAddress address{
            .type = ENET_ADDRESS_TYPE_IPV4, 
            .port = gServer_data.port
        };

        host = enet_host_create (ENET_ADDRESS_TYPE_IPV4, &address, 50ull/* max peer count */, 2ull, 0u, 0u);
        std::thread(&https::listener).detach();
    } // @note delete address
    host->usingNewPacketForServer = true;
    host->checksum = enet_crc32;
    enet_host_compress_with_range_coder(host);

    gDb_config.init();
    mysql_connect();
    decode_items();      // @note reads items.dat into legible class members (id, item name, ect)
    parse_store();       // @todo thread loop this so the store can update without restarting server (stored in .\resource\store.txt)
    parse_chat_filter(); // @note loads resources/chat_filter.txt (optional)
    check_for_holiday(); // @note check for any holidays using local time (your VPS or local time) - @todo thread loop so it can change the holiday without restarting

    ENetEvent event{};
    auto db_ping_at = std::chrono::steady_clock::now();
    auto world_save_at = std::chrono::steady_clock::now() + std::chrono::minutes(5);
    auto duct_tape_check_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!gSignal)
    {
        while (enet_host_service(host, &event, 1000/*ms*/) > 0)
            if (const auto i = eventType_pool.find(event.type); i != eventType_pool.end())
                i->second(event);

        // @note every ~5min: autosave all in-memory worlds (crash safety)
        if (std::chrono::steady_clock::now() > world_save_at)
        {
            for (::world &w : worlds) w.save();
            if (g_verbose) printf("[world] autosaved %zu world(s)\n", worlds.size());
            world_save_at = std::chrono::steady_clock::now() + std::chrono::minutes(5);
        }

        // @note every ~5s: duct tape expires after 10 minutes
        if (std::chrono::steady_clock::now() > duct_tape_check_at)
        {
            for (ENetPeer *p : peers())
            {
                ::peer *o = static_cast<::peer*>(p->data);
                if (o != nullptr && o->state & S_DUCT_TAPE && ticks() - o->duct_tape_tick >= 600)
                {
                    o->state &= ~S_DUCT_TAPE;
                    on::SetClothing(*p);
                }
            }
            duct_tape_check_at = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        }

        // @note every ~30s: watchdog the MariaDB connection and reconnect when it died
        if (std::chrono::steady_clock::now() > db_ping_at)
        {
            if (mysql_ping(db))
            {
                printf("[MariaDB] connection lost, reconnecting...\n");
                mysql_close(db);
                mysql_connect();
            }
            db_ping_at = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        }
    }

    safe_disconnect_peers(gSignal);
    mysql_close(db); // @note deletes db (MYSQL* allocation)
    mysql_library_end();

    puts("killed gurotopia! [EXIT_SUCCESS]");
    return EXIT_SUCCESS;
}