#include "pch.hpp"
#include "tools/create_dialog.hpp"
#include "automate/trade.hpp"

#include "trade_msg.hpp"

static bool as_bool(const std::string &value)
{
    return value == "1" || value == "true";
}

void action::mod_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };

    try
    {
        const int itemID = std::stoi(hPipe["itemID"]);

        const ::item &item = id_to_item(itemID);
        if (item.cat & CAT_UNTRADEABLE)
        {
            send_varlist(event.peer, { "OnTextOverlay", "You can't trade that." });
            trade::play_sfx_for(*pPeer, "bleep_fail.wav");
            return;
        }

        /* a stack needs a quantity pick first - same flow as dropping items */
        int held = 0;
        for (const ::slot &slot : pPeer->slots)
            if (slot.id == itemID) held += slot.count;

        printf("[trade] mod_trade: %s item %d (held %d)\n", pPeer->growid.c_str(), itemID, held);

        if (held > 1)
        {
            send_varlist(event.peer, {
                "OnDialogRequest",
                create_dialog()
                    .set_default_color("`o")
                    .add_label_with_icon("big", std::format("`wTrade {}``", item.raw_name), item.id)
                    .add_textbox("How many to trade?")
                    .add_text_input("count", "", static_cast<short>(std::min(held, 200)), 5)
                    .embed_data("itemID", item.id)
                    .end_dialog("trade_item")
            });
            return;
        }

        int count = 1;
        if (!hPipe["count"].empty()) count = std::stoi(hPipe["count"]);
        trade::add_item(*pPeer, itemID, count);
    }
    catch (...) { printf("[trade] mod_trade: unparsable payload (%.120s)\n", header.c_str()); }
}

void action::rem_trade(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };

    try { trade::remove_item(*pPeer, std::stoi(hPipe["itemID"])); }
    catch (...) { printf("[trade] rem_trade: unparsable payload (%.120s)\n", header.c_str()); }
}

void action::trade_accept(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };

    /* unticking accept applies immediately */
    if (!as_bool(hPipe["status"]))
    {
        trade::set_accept(*pPeer, false);
        return;
    }

    if (pPeer->trade.partner == nullptr) return;

    /* @note modern confirmation dialog: popup title, deal header, one icon row per offered
       stack on both sides, warnings, and a labelled Cancel/Accept pair. Every command here
       is already proven to render on the phone (wrench menu / gazette / drop dialogs use
       add_popup_name, add_custom_break, add_smalltext, add_label_with_icon, end_dialog).
       The client answers dialog_return -> dialog_name|trade_accept (+ buttonClicked). */
    const ::peer &partner = *pPeer->trade.partner;

    /* one visual row per offered stack: the real item icon with a count badge
       (add_button_with_icon is the documented modern shape). Row ids start with item_ so the
       dialog_return handler can tell them apart from the confirmation buttons. */
    auto rows_of = [](const ::peer &who)
    {
        std::string rows{};
        for (const ::slot &slot : who.trade.items)
        {
            const ::item &item = id_to_item(slot.id);
            rows += std::format("add_button_with_icon|item_{}|`w{}`` `ox{}`8 rarity {}``|left|{}|{}|\n",
                                slot.id, item.raw_name, slot.count, item.rarity, slot.id, slot.count);
        }
        if (rows.empty()) rows = "add_smalltext|`8- nothing offered -``|left|\n";
        return rows;
    };

    /* header rows: name + level + xp for both traders (modern add_player_info widget) */
    auto info_of = [](const ::peer &who)
    {
        const int lvl = (who.level.size() >= 2) ? static_cast<int>(who.level.front()) : 1;
        const int xp  = (who.level.size() >= 2) ? static_cast<int>(who.level.back()) : 0;
        return std::tuple{ std::string{who.growid}, lvl, xp, 50 * (lvl * lvl + 2) };
    };

    auto stack_note = [](const ::peer &who)
    {
        const std::size_t n = who.trade.items.size();
        return std::format("({} stack{})", n, n == 1 ? "" : "s");
    };

    send_varlist(event.peer, {
        "OnDialogRequest",
        std::format(
            "set_default_color|`o\n"
            "add_popup_name|TradeConfirm\n"
            "add_label_with_icon|big|`wAccept this trade?``|left|9640|\n"
            "add_smalltext|`oDeal between `w{}`` and `w{}`` - review both sides before it becomes final.``|left|\n"
            "add_custom_break|\n"
            "add_player_info|{}|{}|{}|{}|\n"
            "add_player_info|{}|{}|{}|{}|\n"
            "add_custom_break|\n"
            "add_textbox|`2You give:`` `8{}``|left|\n"
            "{}"
            "add_spacer|small|\n"
            "add_textbox|`2You get:`` `8{}``|left|\n"
            "{}"
            "add_custom_break|\n"
            "add_smalltext|`4Once both sides accept the trade is final - items cannot be recovered.``|left|\n"
            "add_smalltext|`5Tip: tap Lock first if you need a moment to double-check.``|left|\n"
            "end_dialog|trade_accept|Cancel|Accept Trade|\n",
            pPeer->growid, partner.growid,
            std::get<0>(info_of(*pPeer)), std::get<1>(info_of(*pPeer)), std::get<2>(info_of(*pPeer)), std::get<3>(info_of(*pPeer)),
            std::get<0>(info_of(partner)), std::get<1>(info_of(partner)), std::get<2>(info_of(partner)), std::get<3>(info_of(partner)),
            stack_note(*pPeer), rows_of(*pPeer),
            stack_note(partner), rows_of(partner)
        )
    });
    trade::play_sfx_for(*pPeer, "page_turn.wav");
    printf("[trade] %s tapped accept - confirmation dialog sent\n", pPeer->growid.c_str());
}

void action::trade_lock(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    ::hPipe hPipe{ header };
    trade::set_lock(*pPeer, as_bool(hPipe["status"]));
}

void action::trade_cancel(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    printf("[trade] %s canceled the trade\n", pPeer->growid.c_str());
    trade::end(*pPeer, "cancel", false);
}

void action::trade_started(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    if (g_verbose) printf("[trade] trade_started from %s (%.100s)\n", pPeer->growid.c_str(), header.c_str());

    /* the client uses this to confirm it opened the window - re-push current state */
    if (pPeer->trade.partner != nullptr) trade::refresh(*pPeer);
}

void action::trade_check_price(ENetEvent& event, const std::string& header)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);
    printf("[trade] %s check_price (%.80s)\n", pPeer->growid.c_str(), header.c_str());
    /* @todo reply once the expected packet shape is known (observed from real client) */
}
