#include "RankedEntryOverlay.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "../domain/EntryCountdown.hpp"

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
