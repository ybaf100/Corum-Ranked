#pragma once

#include "../domain/ResultScorePresentation.hpp"
#include "RankBadgeAssets.hpp"
#include <Geode/Geode.hpp>
#include <Geode/cocos/draw_nodes/CCDrawNode.h>
#include <functional>

namespace corum::ranked::ui {

class ResultScoreLayer final : public cocos2d::CCLayerColor {
public:
    static ResultScoreLayer* create(ScoreAnimationInput input, cocos2d::CCNode* resultScreen,
        std::function<void()> completion);

private:
    bool init(ScoreAnimationInput input, cocos2d::CCNode* resultScreen, std::function<void()> completion);
    void onEnter() override;
    void onExit() override;
    void update(float dt) override;
    void keyBackClicked() override;
    void onSkip(cocos2d::CCObject*);
    void finish();
    void applySample(ScoreProgressSample const& sample);
    void replaceTierIcons(ScoreProgressSample const& sample, bool animate);
    void retireIcon(cocos2d::CCLayerColor* icon, bool demotion);
    cocos2d::CCLayerColor* tierIcon(std::string const& tier, cocos2d::CCPoint position, bool assemble);
    cocos2d::CCLayerColor* targetIcon(ScoreProgressSample const& sample, bool assemble);
    void showSeries(bool entering);
    void showNotices();
    void stamp(cocos2d::CCNode* node, float delay);

    ScoreAnimationInput m_input;
    std::function<void()> m_completion;
    cocos2d::CCMenu* m_menu = nullptr;
    cocos2d::CCDrawNode* m_track = nullptr;
    cocos2d::CCDrawNode* m_fill = nullptr;
    cocos2d::CCLayerColor* m_leftIcon = nullptr;
    cocos2d::CCLayerColor* m_rightIcon = nullptr;
    cocos2d::CCLabelBMFont* m_score = nullptr;
    cocos2d::CCLabelBMFont* m_delta = nullptr;
    cocos2d::CCLabelBMFont* m_leftTier = nullptr;
    cocos2d::CCLabelBMFont* m_rightTier = nullptr;
    cocos2d::CCLabelBMFont* m_leftCutoff = nullptr;
    cocos2d::CCLabelBMFont* m_rightCutoff = nullptr;
    cocos2d::CCNode* m_rightRank = nullptr;
    cocos2d::CCNode* m_seriesRoot = nullptr;
    std::string m_tier, m_targetKey;
    float m_barLeft = 0, m_barWidth = 0, m_barY = 0, m_iconY = 0;
    double m_elapsed = 0;
    bool m_complete = false, m_closed = false, m_seriesShown = false;
};

} // namespace corum::ranked::ui
