#pragma once

#include <string_view>

// UART 提取与短信任务使用同一份来电分类，避免某种上报在入队前就被过滤。
namespace idf_call_urc {

inline bool ringing(std::string_view line)
{
    if (line == "RING") return true;
    if (line.substr(0, 7) != "+CRING:") return false;
    line.remove_prefix(7);
    std::size_t first = line.find_first_not_of(" \t\"");
    if (first == std::string_view::npos) return false;
    std::size_t last = line.find_last_not_of(" \t\"");
    return line.substr(first, last - first + 1) == "VOICE";
}

inline bool incoming(std::string_view line)
{
    return ringing(line) || line.substr(0, 6) == "+CLIP:";
}

inline bool ended(std::string_view line)
{
    return line == "NO CARRIER" || line == "BUSY" || line == "NO ANSWER";
}

inline bool relevant(std::string_view line)
{
    return incoming(line) || ended(line);
}

}  // namespace idf_call_urc
