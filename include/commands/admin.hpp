#pragma once

/* consolidated administration commands (kept in one unit by design).
   Gating: kick/ban/unban/mute >= MODERATOR, give/setrole >= DEVELOPER. */

void give(ENetEvent& event, const std::string_view text);
void setrole(ENetEvent& event, const std::string_view text);
void kick(ENetEvent& event, const std::string_view text);
void ban(ENetEvent& event, const std::string_view text);
void unban(ENetEvent& event, const std::string_view text);
void mute(ENetEvent& event, const std::string_view text);

/* @return true when the caller role is >= minimum; otherwise sends the
   permission-denied message and returns false. */
extern bool require_role(ENetEvent& event, ::role minimum);
