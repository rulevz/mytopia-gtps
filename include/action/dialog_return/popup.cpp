#include "pch.hpp"

#include "onVariant/ConsoleMessage.hpp"
#include "automate/trade.hpp"

#include "popup.hpp"

void popup(ENetEvent& event, const ::hPipe &hPipe)
{
    ::peer *pPeer = static_cast<::peer*>(event.peer->data);

    if (hPipe["buttonClicked"] == "my_worlds")
    {
        auto section = [](const auto &range) 
        {
            std::string result;
            for (const std::string &name : range)
                if (!name.empty())
                    result.append(std::format("add_button|{0}|{0}|noflags|0|0|\n", name));
            return result;
        };
        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "start_custom_tabs|\n"
                "add_custom_button|myWorldsUiTab_0|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:1,0;width:0.15;|\n"
                "add_custom_button|myWorldsUiTab_1|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:0,1;width:0.15;|\n"
                "add_custom_button|myWorldsUiTab_2|image:interface/large/btn_tabs2.rttex;image_size:228,92;frame:0,2;width:0.15;|\n"
                "end_custom_tabs|\n"
                "add_label|big|Locked Worlds|left|0|\n"
                "add_spacer|small|\n"
                "add_textbox|Place a World Lock in a world to lock it. Break your World Lock to unlock a world.|left|\n"
                "add_spacer|small|\n"
                "{}\n"
                "add_spacer|small|\n"
                "end_dialog|worlds_list||Back|\n"
                "add_quick_exit|\n",
                section(pPeer->my_worlds)
            )
        });
    }
    else if (hPipe["buttonClicked"] == "billboard_edit")
    {
        const ::item &item = id_to_item(pPeer->billboard.id);

        send_varlist(event.peer, {
            "OnDialogRequest",
            std::format(
                "set_default_color|`o\n"
                "add_label_with_icon|big|`wTrade Billboard``|left|8282|\n"
                "add_spacer|small|\n"
                "{}"
                "add_item_picker|billboard_item|`wSelect Billboard Item``|Choose an item to put on your billboard!|\n"
                "add_spacer|small|\n"
                "add_checkbox|billboard_toggle|`$Show Billboard``|{}\n"
                "add_checkbox|billboard_buying_toggle|`$Is Buying``|{}\n"
                "add_text_input|setprice|Price of item:|{}|5|\n"
                "add_checkbox|chk_peritem|World Locks per Item|{}\n"
                "add_checkbox|chk_perlock|Items per World Lock|{}\n"
                "add_spacer|small|\n"
                "end_dialog|billboard_edit|Close|Update|\n",
                (pPeer->billboard.id == 0) ? 
                    "" : 
                    std::format("add_label_with_icon|small|`w{}``|left|{}|\n", item.raw_name, pPeer->billboard.id),
                to_char(pPeer->billboard.show),
                to_char(pPeer->billboard.isBuying),
                pPeer->billboard.price,
                to_char(pPeer->billboard.perItem),
                to_char(!pPeer->billboard.perItem)
            )
        });
    }
    else if (hPipe["buttonClicked"] == "seed_diary_customization")
    {
        send_varlist(event.peer, { "OnDialogRequestRML", "show_seed_diary_ui" });
    }
    else if (hPipe["buttonClicked"] == "trade")
    {
        /* @note player wrench dialog -> Trade button */
        std::string netid_s = hPipe["netID"];
        if (netid_s.empty()) netid_s = hPipe["netid"];
        if (netid_s.empty() || pPeer->netid == 0) return; // @note not in a world / no target in dialog

        try
        {
            const short netid = static_cast<short>(std::stoi(netid_s));
            bool found = false;
            peers(pPeer->recent_worlds.back(), PEER_SAME_WORLD, [&](ENetPeer &other)
            {
                if (found) return;
                ::peer *pOther = static_cast<::peer*>(other.data);
                if (pOther != nullptr && pOther->netid == netid && pOther->user_id != pPeer->user_id)
                {
                    trade::start(*pPeer, *pOther);
                    found = true;
                }
            });
            if (!found) on::ConsoleMessage(event.peer, "`4That person is not here anymore.``");
        }
        catch (...) { printf("[trade] popup: unparsable netID\n"); }
    }
}