#pragma once

#include <array>
#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace corum::ranked {
struct MapDifficulty {
    std::string_view label = "N/A";
    std::string_view frame = "diffIcon_00_btn_001.png";
    int nativeCode=0;
    bool operator==(MapDifficulty const&) const = default;
};

// Native GD metadata, never the separate Corum numeric difficulty/rating.
inline MapDifficulty mapDifficulty(bool autoLevel, bool demon, int demonDifficulty, int average) {
    if (autoLevel) return {"Auto", "diffIcon_auto_btn_001.png",-1};
    if (demon) {
        switch (demonDifficulty) {
            case 3: return {"Easy Demon", "diffIcon_07_btn_001.png",7};
            case 4: return {"Medium Demon", "diffIcon_08_btn_001.png",8};
            case 5: return {"Insane Demon", "diffIcon_09_btn_001.png",9};
            case 6: return {"Extreme Demon", "diffIcon_10_btn_001.png",10};
            default: return {"Hard Demon", "diffIcon_06_btn_001.png",6};
        }
    }
    static constexpr std::array<MapDifficulty, 6> ordinary {{
        {}, {"Easy", "diffIcon_01_btn_001.png",1}, {"Normal", "diffIcon_02_btn_001.png",2},
        {"Hard", "diffIcon_03_btn_001.png",3}, {"Harder", "diffIcon_04_btn_001.png",4},
        {"Insane", "diffIcon_05_btn_001.png",5},
    }};
    return average >= 1 && average <= 5 ? ordinary[average] : ordinary[0];
}
// A recorded difficulty is independent of the live GD metadata cache.
inline std::optional<MapDifficulty> recordedMapDifficulty(std::string name) {
    std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c=='_'?' ':static_cast<char>(std::toupper(c));});
    if(name=="AUTO")return mapDifficulty(true,false,0,0);
    if(name=="EASY DEMON")return mapDifficulty(false,true,3,0);
    if(name=="MEDIUM DEMON")return mapDifficulty(false,true,4,0);
    if(name=="HARD DEMON"||name=="DEMON")return mapDifficulty(false,true,0,0);
    if(name=="INSANE DEMON")return mapDifficulty(false,true,5,0);
    if(name=="EXTREME DEMON")return mapDifficulty(false,true,6,0);
    for(int i=1;i<=5;++i)if(name==std::array<std::string_view,5>{"EASY","NORMAL","HARD","HARDER","INSANE"}[i-1])return mapDifficulty(false,false,0,i);
    return {};
}
} // namespace corum::ranked
