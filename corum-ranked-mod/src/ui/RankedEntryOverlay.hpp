#pragma once
#include <Geode/Geode.hpp>
#include <optional>
#include <cstdint>

namespace corum::ranked::ui {
class RankedEntryOverlay final : public cocos2d::CCLayerColor {
public:
    static RankedEntryOverlay* create();
    void present(bool bothEntered, std::optional<std::int64_t> remainingMs);
    void dismiss();
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override { return true; }
    void keyBackClicked() override {}
private:
    cocos2d::CCLabelBMFont* m_heading = nullptr;
    cocos2d::CCLabelBMFont* m_digit = nullptr;
};
} // namespace corum::ranked::ui
