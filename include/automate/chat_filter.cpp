#include "pch.hpp"
#include <fstream>

#include "chat_filter.hpp"

std::vector<std::string> chat_filter_words{};

void parse_chat_filter()
{
    std::ifstream file("resources/chat_filter.txt");
    for (std::string line; std::getline(file, line); )
    {
        if (line.empty() || line.starts_with("#")) continue;
        for (char &c : line) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        chat_filter_words.emplace_back(std::move(line));
    }
    printf("chat filter: %zu word(s) loaded\n", chat_filter_words.size());
}

bool chat_filter_blocked(const std::string &text)
{
    if (chat_filter_words.empty()) return false;

    std::string lower = text;
    for (char &c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    for (const std::string &word : chat_filter_words)
        if (lower.find(word) != std::string::npos) return true;

    return false;
}
