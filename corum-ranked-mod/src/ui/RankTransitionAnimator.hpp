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

// One short-lived controller is created for each player card that has an
// authoritative tier transition. It owns every action/sprite used by the
// effect, so removing the result scene cancels the entire animation without
// leaving callbacks or scheduled frame updates behind.
class RankTransitionAnimator final : public cocos2d::CCNode {
public:
    static RankTransitionAnimator* create();

    bool playRankTransition(
        std::string oldRank,
        std::string newRank,
        RankTransitionType type,
        cocos2d::CCPoint position,
        std::string eventText,
        std::function<void()> completion = {}
    );

    void cancel();

private:
    bool init() override;

    void playPlacement(std::string const& oldRank, std::string const& newRank);
    void playPromotion(std::string const& oldRank, std::string const& newRank);
    void playDemotion(std::string const& oldRank, std::string const& newRank);

    cocos2d::CCSprite* makeBadge(std::string const& rank, float size);
    cocos2d::CCSprite* makeGlow(std::string const& rank, float size, GLubyte opacity);
    void addInwardParticles(std::string const& rank, int count, float radius, float startDelay);
    void addBurstParticles(std::string const& rank, int count, float distance, float startDelay, bool falling = false);
    void addFlash(std::string const& rank, GLubyte peakOpacity, float startDelay, float size = 94.0f);
    void addLightSweep(std::string const& rank, float startDelay);
    void addCaption(std::string const& text, std::string const& rank, float startDelay);
    void runShake(float strength, float startDelay);
    void finishAfter(float duration);

    std::function<void()> m_completion;
    std::string m_eventText;
    bool m_playing = false;
};

} // namespace corum::ranked::ui
