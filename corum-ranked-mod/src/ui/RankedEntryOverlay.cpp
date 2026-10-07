#include "RankedEntryOverlay.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "../domain/EntryCountdown.hpp"
#include <Geode/binding/UILayer.hpp>

using namespace geode::prelude;
namespace corum::ranked::ui {
RankedEntryOverlay* RankedEntryOverlay::create() {
    auto* overlay = new RankedEntryOverlay;
    if (!overlay->initWithColor({0, 0, 0, 165})) { delete overlay; return nullptr; }
    overlay->autorelease();
    auto const size = CCDirector::sharedDirector()->getWinSize();
    overlay->m_heading = label("", 0.48f, {size.width / 2, size.height / 2 + 36}, Text, FontWeight::Black);
    corum::ranked::ui::fitLabelWidth(overlay->m_heading, size.width - 48, 0.48f, 0.22f);
    overlay->addChild(overlay->m_heading);
    overlay->m_digit = label("", 1.7f, {size.width / 2, size.height / 2 - 15}, Text,
        FontWeight::Black, countdownFont());
    overlay->addChild(overlay->m_digit);
    overlay->setTouchEnabled(true);
    overlay->setKeypadEnabled(true);
    overlay->setID("ranked-entry-countdown"_spr);
    return overlay;
}
void RankedEntryOverlay::registerWithTouchDispatcher() {
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -500, true);
}
void RankedEntryOverlay::dismiss() {
    // Release the blocker before restoring gameplay/returning to a retained
    // round scene, including entry cancellation rather than only successful GO.
    cancelPauseTouch();
    m_play = nullptr;
    setTouchEnabled(false);
    setKeypadEnabled(false);
    removeFromParentAndCleanup(true);
}
void RankedEntryOverlay::releaseToGameplay(PlayLayer* play) {
    // Keep the scene-owned delegate already installed for entry. At GO it
    // stops swallowing gameplay and observes only the existing native Pause
    // button. This avoids relying on a retained CCMenu's touch/callback state.
    // A finger held through the countdown must not become a Pause click.
    cancelPauseTouch();
    m_holding = false;
    m_play = play;
    setOpacity(0);
    m_heading->setVisible(false);
    m_digit->setVisible(false);
    setKeypadEnabled(false);
    log::info("[RankedPauseInput] scene-owned native button ready");
}
bool RankedEntryOverlay::pauseHit(PlayLayer* play, CCTouch* touch) const {
    if (!play || play != PlayLayer::get() || !play->isRunning() || play->m_isPaused ||
        !play->canPauseGame() || !play->m_uiLayer || !play->m_uiLayer->m_pauseBtn) return false;
    auto* button = play->m_uiLayer->m_pauseBtn;
    for (CCNode* node = button; node; node = node->getParent()) {
        if (!node->isVisible()) return false;
        if (node == play) {
            auto const size = button->getContentSize();
            return CCRect(0, 0, size.width, size.height).containsPoint(
                button->convertToNodeSpace(touch->getLocation()));
        }
    }
    return false;
}
void RankedEntryOverlay::cancelPauseTouch() {
    if (m_pauseTouchId != -1) {
        if (auto play = m_play.lock(); play && play->m_uiLayer && play->m_uiLayer->m_pauseBtn)
            play->m_uiLayer->m_pauseBtn->unselected();
    }
    m_pauseTouchId = -1;
}
bool RankedEntryOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (m_holding) return true;
    auto play = m_play.lock();
    if (m_pauseTouchId != -1 || !pauseHit(play.data(), touch)) return false;
    m_pauseTouchId = touch->getID();
    play->m_uiLayer->m_pauseBtn->setEnabled(true);
    play->m_uiLayer->m_pauseBtn->selected();
    return true;
}
void RankedEntryOverlay::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_holding || touch->getID() != m_pauseTouchId) return;
    auto play = m_play.lock();
    auto const activate = pauseHit(play.data(), touch);
    cancelPauseTouch();
    if (!activate) return;
    // doPause resets native touch IDs/UI state, calls PlayLayer::pauseGame and
    // increments the vanilla pause counter. PauseLayer and its resume/restart
    // behavior remain native; RankedPauseLayer adds its existing lease/UI.
    play->m_uiLayer->doPause();
    if (m_pauseDiagnostics++ < 5)
        log::info("[RankedPauseInput] native dispatch paused={} canPause={}",
            play->m_isPaused, play->canPauseGame());
}
void RankedEntryOverlay::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    if (touch->getID() == m_pauseTouchId) cancelPauseTouch();
}
void RankedEntryOverlay::present(bool bothEntered, std::optional<std::int64_t> remainingMs) {
    auto const frame = entryCountdownFrame(remainingMs);
    auto const korean = i18n::isKorean();
    m_heading->setString(bothEntered
        ? (korean ? "시작까지..." : "STARTS IN...")
        : (korean ? "상대 입장 대기 중..." : "WAITING FOR OPPONENT..."));
    m_digit->setString(frame.digit > 0 ? std::to_string(frame.digit).c_str() : "");
    setLabelScale(m_digit, 1.7f * frame.scale);
    m_digit->setOpacity(frame.opacity);
}
} // namespace corum::ranked::ui
