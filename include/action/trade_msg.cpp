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

    /* one icon row per offered stack, built only from commands this client renders well
       (add_label_with_icon is what the wrench menu / gazette already use). The heavy widgets
       (add_button_with_icon tiles, add_player_info blocks) looked cluttered in a small
       dialog, so the rows stay label-based with the real item icon. */
    auto rows_of = [](const ::peer &who)
    {
        std::string rows{};
        for (const ::slot &slot : who.trade.items)
            rows += std::format("add_label_with_icon|small|`w{}`` `ox{}``|left|{}|\n",
                                id_to_item(slot.id).raw_name, slot.count, slot.id);
        if (rows.empty()) rows = "add_smalltext|`8- nothing offered -``|left|\n";
        return rows;
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
