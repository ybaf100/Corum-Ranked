#pragma once
#include "../SurvivalRuntime.hpp"
#include "RankedVisuals.hpp"
#include "RankedHudLabel.hpp"
#include "../domain/HudLayout.hpp"
#include <map>
#include <unordered_map>

namespace corum::ranked::ui {
class SurvivalHud final : public cocos2d::CCNode {
public:
    static SurvivalHud* create();
    void present(SurvivalMatchView const& view,std::int64_t remainingMs,float dt);
    void attachFpsLabel(cocos2d::CCLabelBMFont* label){m_fps=geode::WeakRef(label);}
    void attachProgressBar(cocos2d::CCSprite* bar,cocos2d::CCSprite* fill,float width,float height);
    void update(float dt) override;
    // Display-only HUD customization; called once when the HUD is built.
    void applyLayout(HudLayoutConfig const& layout);
private:
    cocos2d::CCNode* group(std::string_view id);
    struct Row { cocos2d::CCNode* node=nullptr;RankedHudLabel rank,name,score;float targetY=0;SurvivalStanding value;SurvivalScoreTween tween;bool visible=false; };
    bool init() override;
    void presentProgressMarkers(SurvivalMatchView const& view);
    std::unordered_map<std::string,Row> m_rows;
    std::string m_context;
    RankedHudLabel m_round,m_timer,m_auxTimer,m_difference,m_rank,m_waiting;
    cocos2d::CCDrawNode* m_cutoffLine=nullptr;
    geode::WeakRef<cocos2d::CCLabelBMFont> m_fps;
    geode::WeakRef<cocos2d::CCSprite> m_progressBar,m_progressFill;
    geode::WeakRef<cocos2d::CCDrawNode> m_progressLines;
    float m_progressWidth=0,m_progressHeight=0;
    int m_lastRank=0;
    std::map<std::string,cocos2d::CCNode*,std::less<>> m_groups;
    bool m_markersVisible=true;
    SurvivalScoreTween m_personalBest;
};
}
