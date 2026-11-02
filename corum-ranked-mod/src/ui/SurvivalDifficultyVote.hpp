#pragma once

#include "../domain/SurvivalVotePresentation.hpp"
#include <Geode/Geode.hpp>
#include <array>
#include <functional>
#include <optional>
#include <string>

namespace corum::ranked::ui {

// "Select your preferred difficulty." Four cards fade in while rising; a click
// is the vote, the other three fade out and the board scales back up. No
// per-player choice or result is ever shown.
class SurvivalDifficultyVote final : public cocos2d::CCNode {
    struct Card {
        CCMenuItemSpriteExtra* item = nullptr;
        cocos2d::CCNodeRGBA* body = nullptr;
        cocos2d::CCNode* chrome = nullptr;
        float baseY = 0.0f;
    };
    std::array<Card, 4> m_cards{};
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    std::function<void(std::string const&)> m_vote;
    SurvivalVoteStage m_stage = SurvivalVoteStage::Hidden;
    float m_entry = 0.0f, m_leave = 0.0f, m_pulse = 0.0f;
    int m_chosen = -1;
    std::string m_statusText;
    bool init(cocos2d::CCSize size, std::function<void(std::string const&)> vote);
    void onCard(cocos2d::CCObject* sender);
public:
    static SurvivalDifficultyVote* create(cocos2d::CCSize size, std::function<void(std::string const&)> vote);
    void present(SurvivalVoteStage stage, std::string const& myBallot, std::optional<std::int64_t> closesInMs,
        int voted, int total, float dt);
    // 0 = board at full size, 1 = board shrunk above the cards.
    float boardShrink() const;
    bool showing() const { return m_stage == SurvivalVoteStage::Choosing || m_stage == SurvivalVoteStage::Chosen; }
};

} // namespace corum::ranked::ui
