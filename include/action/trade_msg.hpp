#pragma once

/* text messages sent by the client while a trade window is open (capture-first;
 * names follow the community reference, exact wire payloads get validated by the
 * gtproxy capture session) */

namespace action
{
    extern void mod_trade(ENetEvent&, const std::string&);
    extern void rem_trade(ENetEvent&, const std::string&);
    extern void trade_accept(ENetEvent&, const std::string&);
    extern void trade_lock(ENetEvent&, const std::string&);
    extern void trade_cancel(ENetEvent&, const std::string&);
    extern void trade_started(ENetEvent&, const std::string&);
}
