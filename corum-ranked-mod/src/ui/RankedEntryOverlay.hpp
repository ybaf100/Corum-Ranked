#pragma once
#include <Geode/Geode.hpp>
#include <optional>
#include <cstdint>
#include <Geode/binding/PlayLayer.hpp>

namespace corum::ranked::ui {
class RankedEntryOverlay final : public cocos2d::CCLayerColor {
public:
    static RankedEntryOverlay* create();
    void present(bool bothEntered, std::optional<std::int64_t> remainingMs);
    void dismiss();
    void releaseToGameplay(PlayLayer* play);
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override {}
private:
    bool pauseHit(PlayLayer* play, cocos2d::CCTouch* touch) const;
    void cancelPauseTouch();
    geode::WeakRef<PlayLayer> m_play;
    bool m_holding = true;
    int m_pauseTouchId = -1;
    unsigned int m_pauseDiagnostics = 0;
    cocos2d::CCLabelBMFont* m_heading = nullptr;
    cocos2d::CCLabelBMFont* m_digit = nullptr;
};
} // namespace corum::ranked::ui
