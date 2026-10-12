#pragma once
#include "../SurvivalRuntime.hpp"
#include "RankedVisuals.hpp"
#include "RankedHudLabel.hpp"
#include <unordered_map>

namespace corum::ranked::ui {
class SurvivalHud final : public cocos2d::CCNode {
public:
    static SurvivalHud* create();
    void present(SurvivalMatchView const& view,std::int64_t remainingMs,float dt);
    void update(float dt) override;
private:
    struct Row { cocos2d::CCNode* node=nullptr;RankedHudLabel rank,name,score;float targetY=0;SurvivalStanding value;SurvivalScoreTween tween;bool visible=false; };
    bool init() override;
    std::unordered_map<std::string,Row> m_rows;
    std::string m_context;
    RankedHudLabel m_round,m_timer,m_difference,m_rank,m_waiting;
    int m_lastRank=0;
};
}
