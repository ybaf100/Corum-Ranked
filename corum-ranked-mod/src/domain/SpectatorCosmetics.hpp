#pragma once
#include <array>
#include <cstdint>
#include <algorithm>
#include <memory>

namespace corum::ranked {
struct SpectatorColor {
    int r=255,g=255,b=255;
    bool operator==(SpectatorColor const&)const=default;
};
struct SpectatorAvatarCosmetics {
    // Cube, Ship, Ball, UFO, Wave, Robot, Spider, Swing. Asset names are never accepted.
    std::array<int,8> icons{1,1,1,1,1,1,1,1};
    SpectatorColor primary{100,190,255},secondary{240,245,255},glowColor{255,255,255};
    bool glow=false;
    int trail=1;
    bool operator==(SpectatorAvatarCosmetics const&)const=default;
};
struct SpectatorCosmetics {
    std::uint64_t revision=1;
    std::array<SpectatorAvatarCosmetics,2> players;
    bool operator==(SpectatorCosmetics const&)const=default;
};
inline bool validSpectatorColor(SpectatorColor const& c){return c.r>=0&&c.r<=255&&c.g>=0&&c.g<=255&&c.b>=0&&c.b<=255;}
inline bool validSpectatorCosmetics(SpectatorCosmetics const& c){
    if(c.revision==0||c.revision>1'000'000)return false;
    for(auto const& p:c.players){
        if(!validSpectatorColor(p.primary)||!validSpectatorColor(p.secondary)||!validSpectatorColor(p.glowColor)||p.trail<1||p.trail>1000)return false;
        for(auto icon:p.icons)if(icon<1||icon>10000)return false;
    }
    return true;
}
} // namespace corum::ranked
