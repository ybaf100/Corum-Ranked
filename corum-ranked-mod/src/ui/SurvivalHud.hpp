#pragma once
#include "../SurvivalRuntime.hpp"
#include "RankedVisuals.hpp"
#include <unordered_map>

namespace corum::ranked::ui {
class SurvivalHud final : public cocos2d::CCNode {
public:
    static SurvivalHud* create();
    void present(SurvivalMatchView const& view,std::int64_t remainingMs,float dt);
private:
    struct Row { cocos2d::CCNode* node=nullptr;cocos2d::CCLabelBMFont* label=nullptr;float targetY=0;SurvivalStanding value;SurvivalScoreTween tween;bool visible=false; };
    bool init() override;
    std::unordered_map<std::string,Row> m_rows;
    std::string m_context;
    cocos2d::CCLabelBMFont *m_round=nullptr,*m_timer=nullptr,*m_difference=nullptr,*m_differenceCaption=nullptr,*m_rank=nullptr,*m_waiting=nullptr;
    int m_lastRank=0;
};
}
