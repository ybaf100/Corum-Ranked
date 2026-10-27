#pragma once
#include "Sha256.hpp"
#include <string_view>
namespace corum::ranked {
// Native resource paths differ across operating systems. Never transmit them.
inline std::string spectatorTrackHash(std::string_view path){
    auto slash=path.find_last_of("/\\");return sha256Text(slash==std::string_view::npos?path:path.substr(slash+1));
}
}
