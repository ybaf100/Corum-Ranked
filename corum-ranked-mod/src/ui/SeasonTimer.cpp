#include "SeasonTimer.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include <cmath>
#include <vector>

using namespace geode::prelude;
namespace corum::ranked::ui {

SeasonTimer* SeasonTimer::create(bool compact) {
    auto* node = new SeasonTimer;
    if (node->init(compact)) { node->autorelease(); return node; }
    delete node; return nullptr;
}

namespace {
constexpr float Tau = 6.28318530718f;

ccColor4F rgba(ccColor3B color, float alpha) { return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha}; }

// Polygons only: CCDrawNode dots/segments use a texcoord shader that leaves a
// visible square behind them under straight-alpha blending.
void disc(CCDrawNode* node, CCPoint center, float radius, ccColor4F color, int segments = 28) {
    std::vector<CCPoint> points;
    points.reserve(static_cast<std::size_t>(segments));
    for (int i = 0; i < segments; ++i) {
        auto const a = Tau * static_cast<float>(i) / static_cast<float>(segments);
        points.push_back(center + CCPoint{std::cos(a) * radius, std::sin(a) * radius});
    }
    node->drawPolygon(points.data(), static_cast<unsigned>(points.size()), color, 0, {0, 0, 0, 0});
}

void ring(CCDrawNode* node, float inner, float outer, ccColor4F color, int segments = 36) {
    for (int i = 0; i < segments; ++i) {
        auto const a = Tau * static_cast<float>(i) / static_cast<float>(segments);
        auto const b = Tau * static_cast<float>(i + 1) / static_cast<float>(segments);
        CCPoint quad[] {{std::cos(a) * inner, std::sin(a) * inner}, {std::cos(a) * outer, std::sin(a) * outer},
            {std::cos(b) * outer, std::sin(b) * outer}, {std::cos(b) * inner, std::sin(b) * inner}};
        node->drawPolygon(quad, 4, color, 0, {0, 0, 0, 0});
    }
}

// A radial bar from r0 to r1 at angle a (clock angle: 0 = 12 o'clock).
void radial(CCDrawNode* node, float angle, float r0, float r1, float halfWidth, ccColor4F color) {
    CCPoint const dir {std::sin(angle), std::cos(angle)}, side {dir.y, -dir.x};
    CCPoint quad[] {dir * r0 - side * halfWidth, dir * r1 - side * halfWidth, dir * r1 + side * halfWidth, dir * r0 + side * halfWidth};
    node->drawPolygon(quad, 4, color, 0, {0, 0, 0, 0});
}
}

bool SeasonTimer::init(bool compact) {
    if (!CCNode::init()) return false;
    setID("ranked-season-timer"_spr);
    // Small stopwatch-style dial: it marks the countdown, the text carries it.
    m_clock = compact ? 6.0f : 7.0f;
    m_face = CCDrawNode::create();
    m_face->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_face, 1);
    m_hand = CCNode::create();
    m_handDraw = CCDrawNode::create();
    m_handDraw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    m_hand->addChild(m_handDraw);
    addChild(m_hand, 2);
    m_cap = CCDrawNode::create();
    m_cap->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_cap, 3);
    m_season = menuLabel("", compact ? 0.20f : 0.23f, {}, TextMuted, FontWeight::ExtraBold);
    m_season->setAnchorPoint({0.0f, 0.5f});addChild(m_season, 4);
    m_remaining = menuLabel("", compact ? 0.24f : 0.28f, {}, Gold, FontWeight::Black);
    m_remaining->setAnchorPoint({0.0f, 0.5f});addChild(m_remaining, 4);
    drawFace(Gold);
    return true;
}

ccColor3B SeasonTimer::toneColor() const {
    return m_tone == SeasonTone::Gold ? Gold : m_tone == SeasonTone::Red ? Red : TextMuted;
}

// Draw nodes have no opacity: every colour carries m_alpha, the opacity the
// labels inherit from the lobby card while it fades.
void SeasonTimer::drawFace(ccColor3B tone) {
    auto const r = m_clock, k = m_alpha;
    m_face->clear();m_handDraw->clear();
    // Tapered hand with a short tail, drawn pointing at 12.
    CCPoint blade[] {{-0.11f * r, -0.22f * r}, {0.11f * r, -0.22f * r}, {0.035f * r, 0.74f * r}, {-0.035f * r, 0.74f * r}};
    m_handDraw->drawPolygon(blade, 4, {0.96f, 0.97f, 1.0f, k}, 0, {0, 0, 0, 0});
    // Soft halo, dark dial, crisp rim.
    ring(m_face, r, r + 1.1f, rgba(tone, 0.16f * k));
    disc(m_face, {}, r, {10 / 255.0f, 19 / 255.0f, 36 / 255.0f, 0.96f * k});
    ring(m_face, r - 0.9f, r, rgba(tone, 1.0f * k));
    // Stopwatch crown and button.
    radial(m_face, 0.0f, r + 0.4f, r + 1.9f, 0.55f, rgba(tone, 1.0f * k));
    radial(m_face, 0.0f, r + 1.9f, r + 2.6f, 1.25f, rgba(tone, 1.0f * k));
    radial(m_face, Tau / 8.0f, r + 0.3f, r + 1.4f, 0.5f, rgba(tone, 0.85f * k));
    // Twelve ticks; quarters are longer.
    for (int i = 0; i < 12; ++i) {
        auto const major = i % 3 == 0;
        radial(m_face, Tau * static_cast<float>(i) / 12.0f, r * (major ? 0.56f : 0.68f), r * 0.80f,
            major ? 0.42f : 0.26f, rgba(tone, (major ? 0.95f : 0.45f) * k));
    }
    m_cap->clear();
    disc(m_cap, {}, r * 0.17f, rgba(tone, 1.0f * k), 14);
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
    // Follow the card's fade: the labels inherit the lobby card opacity.
    auto const alpha = static_cast<float>(m_season->getDisplayedOpacity()) / 255.0f;
    if (countdown.tone != m_tone || std::abs(alpha - m_alpha) > 0.01f) {
        m_tone = countdown.tone;m_alpha = alpha;drawFace(toneColor());
    }
    auto const tone = toneColor();
    if (label != m_seasonText) { m_seasonText = label; m_season->setString(label.c_str()); }
    if (remaining != m_remainingText) { m_remainingText = remaining; m_remaining->setString(remaining.c_str()); }
    m_remaining->setColor(tone);
    // Lay out [clock] label remaining, centred on this node.
    auto const dialGap = 5.0f, textGap = 5.0f, dial = m_clock + 1.1f;
    auto const seasonWidth = m_season->getScaledContentSize().width, remainingWidth = m_remaining->getScaledContentSize().width;
    auto const total = dial * 2 + dialGap + seasonWidth + textGap + remainingWidth;
    auto x = -total / 2.0f;
    m_face->setPosition({x + dial, -0.6f});m_hand->setPosition({x + dial, -0.6f});m_cap->setPosition({x + dial, -0.6f});
    x += dial * 2 + dialGap;
    m_season->setPosition({x, 0});x += seasonWidth + textGap;
    m_remaining->setPosition({x, 0});
    m_hand->setRotation(seasonHandAngle(serverNowMs));
    return true;
}

} // namespace corum::ranked::ui
