#include "RankedEntryOverlay.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "RankedInput.hpp"
#include "../domain/EntryCountdown.hpp"
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/AppDelegate.hpp>
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
    { [[maybe_unused]] auto retiredOwner = std::move(m_play); }
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
    m_play = geode::WeakRef<PlayLayer>(play);
    synchronizeTouchPriority(play);
    setOpacity(0);
    m_heading->setVisible(false);
    m_digit->setVisible(false);
    setKeypadEnabled(false);
    auto* handler = CCDirector::sharedDirector()->getTouchDispatcher()->findHandler(this);
    log::info("[RankedPauseInput] scene-owned native button ready priority={}",
        handler ? handler->m_nPriority : 0);
}
void RankedEntryOverlay::synchronizeTouchPriority(PlayLayer* play) {
    if (!play || play != PlayLayer::get() || !play->m_uiLayer || !play->m_uiLayer->m_pauseBtn) return;
    auto* menu = typeinfo_cast<CCMenu*>(play->m_uiLayer->m_pauseBtn->getParent());
    alignPauseObserverPriority(this, play->m_uiLayer, menu);
}
bool RankedEntryOverlay::pauseHit(PlayLayer* play, CCTouch* touch) const {
    if (!play || play != PlayLayer::get() || !play->isRunning() || play->m_isPaused ||
        !play->canPauseGame() || !play->m_uiLayer || play->m_uiLayer->m_gameLayer != play ||
        !play->m_uiLayer->m_pauseBtn) return false;
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
    auto* ui = play->m_uiLayer;
    auto const counterBefore = play->m_gameState.m_pauseCounter;
    auto const bufferBefore = play->m_gameState.m_pauseBufferTimer;
    auto const buttonTargetMatches = ui->m_pauseBtn->m_pListener == ui;
    auto const selectorPresent = ui->m_pauseBtn->m_pfnSelector != nullptr;
    auto const baseOwnerMatches = GameManager::sharedState()->m_gameLayer == play.data();
    auto const appPausedBefore = AppDelegate::get()->m_gamePaused;
    // Keep the installed button callback first. beta40 device evidence proves
    // that returning from activate() does NOT imply that native Pause opened.
    ui->m_pauseBtn->activate();
    auto const buttonPaused = play->m_isPaused;
    bool nativeFallback = false;
    if (!buttonPaused && !m_holding && play->m_uiLayer == ui && pauseHit(play.data(), touch)) {
        // Recheck exact ownership/canPause after the callback: it may have
        // completed, detached or replaced this scene. Use the same native
        // PlayLayer entry point used by Geode's PopupManager, never fabricate
        // m_isPaused/PauseLayer or retry a successful callback.
        nativeFallback = true;
        ui->resetAllButtons();
        play->pauseGame(false);
        // UILayer::doPause normally increments this counter. Preserve a
        // callback's increment; count a successful direct request only once.
        if (play->m_isPaused && play->m_gameState.m_pauseCounter == counterBefore)
            ++play->m_gameState.m_pauseCounter;
    }
    if (m_pauseDiagnostics++ < 5)
        log::info("[RankedPauseInput] request level={} buttonPaused={} nativeFallback={} paused={} canPause={} uiOwnerMatches={} baseOwnerMatches={} buttonTargetMatches={} selectorPresent={} pauseCounterBefore={} pauseCounterAfter={} pauseBufferBefore={} appPausedBefore={} appPausedAfter={}",
            play->m_level ? static_cast<int>(play->m_level->m_levelID) : 0,
            buttonPaused, nativeFallback, play->m_isPaused, play->canPauseGame(),
            play->m_uiLayer == ui, baseOwnerMatches, buttonTargetMatches, selectorPresent,
            counterBefore, play->m_gameState.m_pauseCounter, bufferBefore,
            appPausedBefore, AppDelegate::get()->m_gamePaused);
}
void RankedEntryOverlay::ccTouchCancelled(CCTouch* touch, CCEvent*) {
    if (touch->getID() == m_pauseTouchId) cancelPauseTouch();
}
void RankedEntryOverlay::present(bool bothEntered, std::optional<std::int64_t> remainingMs) {
    // The native child delegates have registered by this scene heartbeat. Keep
    // the countdown blocker ahead of them too, including rounds after modals.
    synchronizeTouchPriority(PlayLayer::get());
    auto const frame = m_countdown.observe(bothEntered ? remainingMs : std::nullopt);
    auto const korean = i18n::isKorean();
    auto const counting = bothEntered || m_countdown.started();
    auto const headingState = static_cast<int>(korean) * 2 + static_cast<int>(counting);
    if (m_headingState != headingState) {
        m_headingState = headingState;
        m_heading->setString(counting
            ? (korean ? "시작까지..." : "STARTS IN...")
            : (korean ? "상대 입장 대기 중..." : "WAITING FOR OPPONENT..."));
    }
    if (m_presentedDigit != frame.digit) {
        m_presentedDigit = frame.digit;
        m_digit->setString(frame.digit > 0 ? std::to_string(frame.digit).c_str() : "");
    }
    setLabelScale(m_digit, 1.7f * frame.scale);
    m_digit->setOpacity(frame.opacity);
}
} // namespace corum::ranked::ui
