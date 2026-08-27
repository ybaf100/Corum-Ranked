#pragma once

#include <Geode/Geode.hpp>

#include <functional>
#include <string>

namespace corum::ranked::ui {

enum class RankTransitionType {
    Placement,
    Promotion,
    Demotion,
};

// Full-screen, presentation-only rank reveal. The caller supplies the
// server-authoritative old/new tiers and event type; this layer never computes
// rank order, LP, MMR, placement, promotion, or demotion outcomes.
class RankTransitionLayer final : public cocos2d::CCLayerColor {
public:
    static RankTransitionLayer* create(
        std::string oldRank,
        std::string newRank,
        RankTransitionType type,
        std::string eventText,
        std::string skipText,
        std::string continueText,
        std::function<void()> completion
    );

private:
    bool init(
        std::string oldRank,
        std::string newRank,
        RankTransitionType type,
        std::string eventText,
        std::string skipText,
        std::string continueText,
        std::function<void()> completion
    );

    void createBackground();
    void createControls();
    void playPlacement();
    void playPromotion();
    void playDemotion();
    void revealFinalState(bool skipped);
    void unlockSkip();
    void onSkip(cocos2d::CCObject*);
    void onContinue(cocos2d::CCObject*);
    void keyBackClicked() override;

    cocos2d::CCLayerColor* makeAssembly(std::string const& rank, float badgeSize, bool ambient);
    cocos2d::CCLayerColor* makeOrnamentGroup(int stage, bool ambient);
    void addStageOrnaments(cocos2d::CCNode* parent, int stage, bool ambient);
    void addParticles(
        std::string const& rank,
        int count,
        float radius,
        float distance,
        float delay,
        bool inward = false,
        bool falling = false
    );
    void addFlash(cocos2d::ccColor3B color, GLubyte opacity, float delay);
    void addLightSweep(cocos2d::ccColor3B color, float delay);
    void runShake(float strength, float delay);
    void scheduleFinalReveal(float duration);

    std::string m_oldRank;
    std::string m_newRank;
    RankTransitionType m_type = RankTransitionType::Promotion;
    std::string m_eventText;
    std::string m_skipText;
    std::string m_continueText;
    std::function<void()> m_completion;

    cocos2d::CCNode* m_animationRoot = nullptr;
    cocos2d::CCNode* m_backgroundRoot = nullptr;
    cocos2d::CCMenu* m_controlMenu = nullptr;
    CCMenuItemSpriteExtra* m_skipItem = nullptr;
    CCMenuItemSpriteExtra* m_continueItem = nullptr;
    bool m_skipAllowed = false;
    bool m_finalShown = false;
    bool m_finished = false;
};

} // namespace corum::ranked::ui
