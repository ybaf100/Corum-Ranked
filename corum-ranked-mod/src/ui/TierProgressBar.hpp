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
    bool init(TierProgressView view, float width, std::string const& promotionTarget);
    void redraw();
public:
    static TierProgressBar* create(TierProgressView view, float width, std::string const& promotionTarget = {});
    void update(float dt) override;
};

} // namespace corum::ranked::ui
