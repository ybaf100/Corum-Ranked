#pragma once

#include "../domain/TierProgress.hpp"
#include <Geode/Geode.hpp>
#include <string>

namespace corum::ranked::ui {

// Lobby tier bar under the matchmaking start button: own tier badge, the
// next tier's badge (or the next rank's on the GOLD ladder), a progress bar
// and how much is left. The ladder leader gets a full rainbow bar.
class TierProgressBar final : public cocos2d::CCNode {
    TierProgressView m_view;
    float m_width = 166.0f, m_time = 0.0f;
    cocos2d::ccColor3B m_tone{255, 207, 74};
    cocos2d::CCDrawNode* m_draw = nullptr;
    cocos2d::CCLabelBMFont* m_caption = nullptr;
    cocos2d::CCNode* m_labels = nullptr;
    // During a tier change: the previous tier's labels until the hand-over.
    cocos2d::CCNode* m_oldLabels = nullptr;
    cocos2d::ccColor3B m_fromTone{255, 207, 74};
    cocos2d::CCNodeRGBA* m_probe = nullptr;
    TierProgressView m_from;
    float m_anim = 0.0f;
    bool m_animating = false, m_up = true, m_tierChange = false;
    // Compact (lobby mode card): no badge/name row, only the bar and its caption.
    bool m_compact = false;
    bool init(TierProgressView view, float width, std::string const& promotionTarget, bool compact);
    cocos2d::CCNode* buildLabels(TierProgressView const& view, std::string const& promotionTarget);
    void redraw();
public:
    static TierProgressBar* create(TierProgressView view, float width, std::string const& promotionTarget = {}, bool compact = false);
    void update(float dt) override;
    void animateFrom(TierProgressView from, bool up);
};

} // namespace corum::ranked::ui
