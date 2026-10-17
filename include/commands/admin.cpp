#include "pch.hpp"
#include <cctype>
#include <cstdlib>
#include "onVariant/ConsoleMessage.hpp"

#include "admin.hpp"

/* @note administration commands: give, setrole, kick, ban, unban, mute.
   Gating: kick/ban/unban/mute >= MODERATOR; give/setrole >= DEVELOPER. */

namespace
{
    ::peer *find_online(const std::string &name)
    {
        for (ENetPeer *p : peers())
        {
            ::peer *o = static_cast<::peer*>(p->data);
            if (o != nullptr && !o->growid.empty() && o->growid == name) return o;
        }
        return nullptr;
    }

    ENetPeer *find_online_net(const std::string &name)
    {
        for (ENetPeer *p : peers())
        {
            ::peer *o = static_cast<::peer*>(p->data);
            if (o != nullptr && !o->growid.empty() && o->growid == name) return p;
        }
        return nullptr;
    }

    void set_column(const std::string &growid, const char *column, int value)
    {
        ::hStmt hStmt{ std::format("UPDATE peer SET {} = ? WHERE growid = ?", column).c_str() };

        const signed v = value;
        MYSQL_BIND params[2] = { make_bind_in(v), make_bind_in(growid) };
        hStmt.bind_param(params);
        hStmt.execute();
    }

    std::string arg_after(const std::string_view text, const std::string_view cmd)
    {
        std::string rest{ text.substr(cmd.size()) };
        rest.erase(rest.begin(), std::find_if_not(rest.begin(), rest.end(), ::isspace));
        return rest;
    }
}

bool require_role(ENetEvent& event, ::role minimum)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (pPeer->role >= minimum) return true;

    send_action(*event.peer, "log", "msg|`4You don't have permission to use this command.``");
    return false;
}

void give(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, DEVELOPER)) return;

    const std::string rest = arg_after(text, "give");
    if (rest.empty()) { send_action(*event.peer, "log", "msg|usage: /give {item id} {count}"); return; }

    char *end = nullptr;
    const long id = std::strtol(rest.c_str(), &end, 10);
    while (*end == ' ') ++end;
    long count = 1;
    if (*end != '\0') count = std::strtol(end, nullptr, 10);

    if (id <= 0 || count <= 0 || count > 200)
    {
        send_action(*event.peer, "log", "msg|usage: /give {item id} {count (1-200)}");
        return;
    }
    modify_item_inventory(event, ::slot{ static_cast<short>(id), static_cast<short>(count) });
    on::ConsoleMessage(event.peer, std::format("Gave {}x item {}.", count, id));
}

void setrole(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, DEVELOPER)) return;

    const std::string rest = arg_after(text, "setrole");
    const std::size_t sp = rest.find(' ');
    if (sp == std::string::npos)
    {
        send_action(*event.peer, "log", "msg|usage: /setrole {growid} {player|moderator|developer}");
        return;
    }

    const std::string target = rest.substr(0, sp);
    std::string r = rest.substr(sp + 1);
    for (char &c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    int value = -1;
    if      (r == "player"    || r == "0") value = PLAYER;
    else if (r == "moderator" || r == "mod" || r == "1") value = MODERATOR;
    else if (r == "developer" || r == "dev" || r == "2") value = DEVELOPER;

    if (value < 0)
    {
        send_action(*event.peer, "log", "msg|usage: /setrole {growid} {player|moderator|developer}");
        return;
    }

    set_column(target, "role", value);
    if (::peer *o = find_online(target)) o->role = static_cast<u_char>(value);
    on::ConsoleMessage(event.peer, std::format("Set role of {} to {}.", target, r));
}

void kick(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, MODERATOR)) return;

    const std::string name = arg_after(text, "kick");
    if (name.empty()) { send_action(*event.peer, "log", "msg|usage: /kick {growid}"); return; }

    ENetPeer *target = find_online_net(name);
    if (target == nullptr) { on::ConsoleMessage(event.peer, std::format("`4{} is not online.``", name)); return; }

    send_action(*target, "log", "msg|`6You have been kicked from the server.");
    enet_peer_disconnect_later(target, 0);
    printf("[admin] kicked '%s'\n", name.c_str()); // @note moderation event, always logged
    on::ConsoleMessage(event.peer, std::format("Kicked {}.", name));
}

void ban(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, MODERATOR)) return;

    const std::string name = arg_after(text, "ban");
    if (name.empty()) { send_action(*event.peer, "log", "msg|usage: /ban {growid}"); return; }

    set_column(name, "banned", 1);
    if (ENetPeer *target = find_online_net(name))
    {
        send_action(*target, "log", "msg|`4You are banned from this server.");
        enet_peer_disconnect_later(target, 0);
    }
    printf("[admin] banned '%s'\n", name.c_str()); // @note moderation event, always logged
    on::ConsoleMessage(event.peer, std::format("Banned {}.", name));
}

void unban(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, MODERATOR)) return;

    const std::string name = arg_after(text, "unban");
    if (name.empty()) { send_action(*event.peer, "log", "msg|usage: /unban {growid}"); return; }

    set_column(name, "banned", 0);
    printf("[admin] unbanned '%s'\n", name.c_str()); // @note moderation event, always logged
    on::ConsoleMessage(event.peer, std::format("Unbanned {}.", name));
}

void mute(ENetEvent& event, const std::string_view text)
{
    if (!require_role(event, MODERATOR)) return;

    const std::string name = arg_after(text, "mute");
    if (name.empty()) { send_action(*event.peer, "log", "msg|usage: /mute {growid}"); return; }

    { // @note toggle in DB
        ::hStmt hStmt{ "UPDATE peer SET muted = 1 - muted WHERE growid = ?" };
        MYSQL_BIND param = make_bind_in(name);
        hStmt.bind_param(&param);
        hStmt.execute();
    }
    bool now_muted = false;
    { // @note read the new value back
        ::hStmt hStmt{ "SELECT muted FROM peer WHERE growid = ? LIMIT 1" };
        MYSQL_BIND param = make_bind_in(name);
        hStmt.bind_param(&param);

        signed v = 0;
        MYSQL_BIND out = make_bind_out(v);
        mysql_stmt_bind_result(hStmt.pStmt, &out);
        hStmt.execute();
        mysql_stmt_store_result(hStmt.pStmt);
        if (mysql_stmt_fetch(hStmt.pStmt) == 0) now_muted = v != 0;
    }
    if (ENetPeer *target = find_online_net(name))
    {
        static_cast<::peer*>(target->data)->muted = now_muted;
        on::ConsoleMessage(target, now_muted ? "`4You have been muted.``" : "`2You have been unmuted.``");
    }
    printf("[admin] '%s' is now %s\n", name.c_str(), now_muted ? "muted" : "unmuted"); // @note moderation event, always logged
    on::ConsoleMessage(event.peer, std::format("{} is now {}.", name, now_muted ? "muted" : "unmuted"));
}
