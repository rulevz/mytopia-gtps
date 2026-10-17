#pragma once

/* @note simple substring-based chat filter, loaded from resources/chat_filter.txt (optional). */

extern std::vector<std::string> chat_filter_words;

/* @return true when the text contains a filtered word. */
extern bool chat_filter_blocked(const std::string &text);

/* load words from resources/chat_filter.txt (case-insensitive, '#' comments). */
extern void parse_chat_filter();
