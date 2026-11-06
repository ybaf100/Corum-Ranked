#include "SeasonTimer.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"

using namespace geode::prelude;
namespace corum::ranked::ui {

SeasonTimer* SeasonTimer::create(bool compact) {
    auto* node = new SeasonTimer;
    if (node->init(compact)) { node->autorelease(); return node; }
    delete node; return nullptr;
}

bool SeasonTimer::init(bool compact) {
    if (!CCNode::init()) return false;
    setID("ranked-season-timer"_spr);
    m_clock = compact ? 9.0f : 11.0f;
    m_face = CCDrawNode::create();
    m_face->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_face, 1);
    m_hand = CCNode::create();
    auto* hand = CCDrawNode::create();
    hand->drawSegment({0, -1.2f}, {0, m_clock * 0.72f}, m_clock * 0.09f, {0.95f, 0.97f, 1.0f, 1.0f});
    m_hand->addChild(hand);
    addChild(m_hand, 2);
    m_season = menuLabel("", compact ? 0.20f : 0.23f, {}, TextMuted, FontWeight::ExtraBold);
    m_season->setAnchorPoint({0.0f, 0.5f});addChild(m_season, 3);
    m_remaining = menuLabel("", compact ? 0.24f : 0.28f, {}, Gold, FontWeight::Black);
    m_remaining->setAnchorPoint({0.0f, 0.5f});addChild(m_remaining, 3);
    drawFace(Gold);
    return true;
}

void SeasonTimer::drawFace(ccColor3B tone) {
    m_face->clear();
    auto const color = ccColor4F{tone.r / 255.0f, tone.g / 255.0f, tone.b / 255.0f, 1.0f};
    m_face->drawDot({}, m_clock, {10 / 255.0f, 19 / 255.0f, 36 / 255.0f, 1.0f});
    constexpr int segments = 36;
    for (int i = 0; i < segments; ++i) {
        auto const a = 6.28318530718f * i / segments, b = 6.28318530718f * (i + 1) / segments;
        m_face->drawSegment({std::cos(a) * m_clock, std::sin(a) * m_clock}, {std::cos(b) * m_clock, std::sin(b) * m_clock}, m_clock * 0.08f, color);
    }
    for (int i = 0; i < 4; ++i) {
        auto const a = 1.57079632679f * i;
        m_face->drawSegment({std::cos(a) * m_clock * 0.62f, std::sin(a) * m_clock * 0.62f},
            {std::cos(a) * m_clock * 0.82f, std::sin(a) * m_clock * 0.82f}, m_clock * 0.07f, {color.r, color.g, color.b, 0.8f});
    }
    m_face->drawDot({}, m_clock * 0.16f, color);
}

bool SeasonTimer::present(SeasonView const& season, bool modeEnabled, std::int64_t serverNowMs) {
    auto const countdown = seasonCountdown(season, modeEnabled, serverNowMs);
    if (countdown.kind == SeasonCountdownKind::Hidden) { setVisible(false); return false; }
    setVisible(true);
    auto const label = season.preseason ? i18n::text("season-preseason", "PRESEASON")
        : fmt::format(fmt::runtime(i18n::text("season-number", "SEASON {}")), season.number);
    std::string remaining;
    switch (countdown.kind) {
        case SeasonCountdownKind::LeftDays: remaining = fmt::format(fmt::runtime(i18n::text("season-left-days", "{}d {}h left")), countdown.days, countdown.hours); break;
        case SeasonCountdownKind::LeftClock: remaining = fmt::format(fmt::runtime(i18n::text("season-left-clock", "{}h {}m {}s left")), countdown.hours, countdown.minutes, countdown.seconds); break;
        case SeasonCountdownKind::Ended: remaining = i18n::text("season-ended", "SEASON ENDED"); break;
        case SeasonCountdownKind::StartsDays: remaining = fmt::format(fmt::runtime(i18n::text("season-starts-days", "Starts in {}d {}h")), countdown.days, countdown.hours); break;
        case SeasonCountdownKind::StartsHours: remaining = fmt::format(fmt::runtime(i18n::text("season-starts-hours", "Starts in {}h {}m")), countdown.hours, countdown.minutes); break;
        default: break;
    }
    auto const tone = countdown.tone == SeasonTone::Gold ? Gold : countdown.tone == SeasonTone::Red ? Red : TextMuted;
    if (countdown.tone != m_tone) { m_tone = countdown.tone; drawFace(tone); }
    if (label != m_seasonText) { m_seasonText = label; m_season->setString(label.c_str()); }
    if (remaining != m_remainingText) { m_remainingText = remaining; m_remaining->setString(remaining.c_str()); }
    m_remaining->setColor(tone);
    // Lay out [clock] label remaining, centred on this node.
    auto const gap = m_clock * 0.55f;
    auto const seasonWidth = m_season->getScaledContentSize().width, remainingWidth = m_remaining->getScaledContentSize().width;
    auto const total = m_clock * 2 + gap + seasonWidth + gap + remainingWidth;
    auto x = -total / 2.0f;
    m_face->setPosition({x + m_clock, 0});m_hand->setPosition({x + m_clock, 0});
    x += m_clock * 2 + gap;
    m_season->setPosition({x, 0});x += seasonWidth + gap;
    m_remaining->setPosition({x, 0});
    m_hand->setRotation(seasonHandAngle(serverNowMs));
    return true;
}

} // namespace corum::ranked::ui
