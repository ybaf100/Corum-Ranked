#include "TierProgressBar.hpp"
#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include "../domain/RankTierPresentation.hpp"
#include "../domain/AchievementStyle.hpp"
#include <chrono>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
constexpr float BarY = -2.0f, BarH = 7.0f;

ccColor4F rgba(ccColor3B color, float alpha) { return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha}; }

void quad(CCDrawNode* node, float x0, float y0, float x1, float y1, ccColor4F color) {
    CCPoint points[] {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    node->drawPolygon(points, 4, color, 0, {0, 0, 0, 0});
}

// The Mythic (V) achievement rainbow: the same seven pastels, flowing along
// the bar once every 3 s.
ccColor4F mythicPastel(float position, double seconds, float alpha) {
    auto const c = achievementPastelAt(position, seconds);
    return {c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, alpha};
}

CCNode* rankChip(int rank) {
    auto* root = CCNode::create();
    root->addChild(createRankBadgeBackdrop("GOLD", {24, 24}, RankBadgeDisplay::Matched), 0);
    auto* text = label(fmt::format("#{}", rank), .20f, {}, Gold, FontWeight::Black);
    fitMenuLabelWidth(text, 22.0f, .20f, .12f);root->addChild(text, 2);
    return root;
}
}

TierProgressBar* TierProgressBar::create(TierProgressView view, float width, std::string const& promotionTarget) {
    auto* node = new TierProgressBar;
    if (node->init(std::move(view), width, promotionTarget)) { node->autorelease();return node; }
    delete node;return nullptr;
}

bool TierProgressBar::init(TierProgressView view, float width, std::string const& promotionTarget) {
    if (!CCNode::init()) return false;
    setID("ranked-tier-progress"_spr);
    m_view = std::move(view);m_width = width;
    m_labels = CCNode::create();addChild(m_labels, 3);
    // Receives the panel fade only (the labels may be dimmed by the bar itself).
    m_probe = CCNodeRGBA::create();m_probe->setCascadeOpacityEnabled(true);addChild(m_probe);
    auto const left = -width / 2.0f, right = width / 2.0f;
    auto const tier = m_view.tier;
    m_tone = tierColor(tier);
    // Top row: own badge and tier, then the next tier (or rank) on the right.
    auto* badge = createAnimatedRankBadge(tier, {18, 18}, RankBadgeDisplay::Matched);
    badge->setPosition({left + 9.0f, 11.0f});m_labels->addChild(badge, 3);
    auto* name = label(rankTierDisplay(tier), .25f, {left + 21.0f, 11.0f}, m_tone, FontWeight::Black);
    name->setAnchorPoint({0.0f, 0.5f});fitMenuLabelWidth(name, width * .48f, .25f, .15f);m_labels->addChild(name, 4);
    std::string next;CCNode* nextBadge = nullptr;
    switch (m_view.kind) {
        case TierProgressKind::Progress: case TierProgressKind::Near: case TierProgressKind::Warning:
            next = rankTierDisplay(m_view.nextTier);nextBadge = createAnimatedRankBadge(m_view.nextTier, {15, 15}, RankBadgeDisplay::Matched);break;
        case TierProgressKind::Promotion:
            if (!promotionTarget.empty()) { next = rankTierDisplay(promotionTarget);nextBadge = createAnimatedRankBadge(promotionTarget, {15, 15}, RankBadgeDisplay::Matched); }
            break;
        case TierProgressKind::RankChase: nextBadge = rankChip(m_view.targetRank);break;
        case TierProgressKind::Leader: nextBadge = rankChip(1);break;
        default: break;
    }
    auto nextX = right;
    if (nextBadge) { nextBadge->setPosition({right - 9.0f, 11.0f});m_labels->addChild(nextBadge, 3);nextX = right - 20.0f; }
    if (!next.empty()) {
        auto* nextLabel = label(next, .20f, {nextX, 11.0f}, TextMuted, FontWeight::ExtraBold);
        nextLabel->setAnchorPoint({1.0f, 0.5f});fitMenuLabelWidth(nextLabel, width * .34f, .20f, .12f);m_labels->addChild(nextLabel, 4);
    }
    // Bottom row: what is left, and the score (or the series marks).
    std::string caption, score;ccColor3B captionColor = Gold;
    switch (m_view.kind) {
        case TierProgressKind::Progress:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-next", "{} LP to next tier")), m_view.remaining);break;
        case TierProgressKind::Near:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-near", "Almost there: {} LP")), m_view.remaining);captionColor = Green;break;
        case TierProgressKind::Warning:
            caption = i18n::text("demotion-warning", "DEMOTION WARNING");captionColor = Red;break;
        case TierProgressKind::Promotion:
            caption = i18n::text("tier-bar-promotion", "PROMOTION SERIES");break;
        case TierProgressKind::RankChase:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-rank", "To #{}: {} LP")), m_view.targetRank, m_view.remaining);break;
        case TierProgressKind::Leader:
            caption = i18n::text("tier-bar-leader", "#1 ON THE LADDER");break;
        case TierProgressKind::Top:
            caption = i18n::text("tier-bar-top", "TOP TIER");break;
        default: break;
    }
    if (m_view.kind == TierProgressKind::Progress || m_view.kind == TierProgressKind::Near || m_view.kind == TierProgressKind::Warning
        || m_view.kind == TierProgressKind::RankChase)
        score = fmt::format("{} / {} LP", m_view.score, m_view.maximum);
    else if (m_view.kind != TierProgressKind::Promotion) score = fmt::format("{} LP", m_view.score);
    m_caption = label(caption, .21f, {left, -14.0f}, captionColor, FontWeight::ExtraBold);
    m_caption->setAnchorPoint({0.0f, 0.5f});fitMenuLabelWidth(m_caption, width * .62f, .21f, .13f);m_labels->addChild(m_caption, 4);
    if (!score.empty()) {
        auto* scoreLabel = label(score, .18f, {right, -14.0f}, TextMuted, FontWeight::SemiBold);
        scoreLabel->setAnchorPoint({1.0f, 0.5f});fitMenuLabelWidth(scoreLabel, width * .36f, .18f, .11f);m_labels->addChild(scoreLabel, 4);
    }
    if (m_view.kind == TierProgressKind::Promotion) {
        // Series marks, right-aligned: won (green), lost (red), left (empty).
        auto const games = std::clamp(m_view.promotionGames, 1, 9);
        for (int i = 0; i < games; ++i) {
            auto const x = right - 4.0f - static_cast<float>(games - 1 - i) * 10.0f;
            auto const result = i < static_cast<int>(m_view.promotionResults.size()) ? m_view.promotionResults[static_cast<std::size_t>(i)] : std::string();
            auto const accent = result == "WIN" ? Green : result.empty() ? TextMuted : Red;
            auto* mark = surface({8, 8}, {x, -14.0f}, accent, result.empty() ? Ink : SurfaceSoft, 255, !result.empty());
            m_labels->addChild(mark, 4);
        }
    }
    m_draw = CCDrawNode::create();
    m_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_draw, 1);
    redraw();
    if (m_view.kind == TierProgressKind::Leader || m_view.kind == TierProgressKind::Promotion) scheduleUpdate();
    else schedule(schedule_selector(TierProgressBar::update), 0.1f);
    return true;
}

void TierProgressBar::update(float dt) {
    m_time += dt;
    if (m_animating) {
        m_anim += dt;
        if (m_anim > 3.2f) { m_animating = false; }
    }
    redraw();
}

// After an LP change: the bar runs from the previous value. Across a tier
// change it first runs to the old band's edge (full gold / empty red), then
// restarts in the new band; the labels show the new tier once it restarts.
void TierProgressBar::animateFrom(TierProgressView from, bool up) {
    m_from = std::move(from);m_up = up;m_anim = 0.0f;m_animating = true;
    m_tierChange = m_from.tier != m_view.tier;
    unschedule(schedule_selector(TierProgressBar::update));scheduleUpdate();
    redraw();
}

// Draw nodes have no opacity: colours carry the caption's inherited opacity,
// so the bar fades with the lobby panel.
void TierProgressBar::redraw() {
    if (!m_draw) return;
    auto const k = m_probe ? static_cast<float>(m_probe->getDisplayedOpacity()) / 255.0f : 1.0f;
    auto const left = -m_width / 2.0f, right = m_width / 2.0f, y0 = BarY - BarH / 2.0f, y1 = BarY + BarH / 2.0f;
    m_draw->clear();
    auto const leader = m_view.kind == TierProgressKind::Leader;
    auto const edgeColor = m_up ? Gold : Red;
    auto const border = edge ? edgeColor : m_view.kind == TierProgressKind::Warning ? Red
        : m_view.kind == TierProgressKind::Promotion || m_view.kind == TierProgressKind::RankChase ? Gold : m_tone;
    // Frame, track.
    quad(m_draw, left - 1.5f, y0 - 1.5f, right + 1.5f, y1 + 1.5f, rgba(border, (leader ? 0.9f : 0.55f) * k));
    quad(m_draw, left, y0, right, y1, {10 / 255.0f, 19 / 255.0f, 36 / 255.0f, k});
    auto fill = std::clamp(m_view.fill, 0.0f, 1.0f);
    auto edge = false, labelsAlpha = 1.0f;
    if (m_animating) {
        auto const seg = [&](float a, float b) { return std::clamp((m_anim - a) / (b - a), 0.0f, 1.0f); };
        auto const ease = [](float x) { return x < 0.5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.0f) / 2; };
        if (!m_tierChange) fill = m_from.fill + (fill - m_from.fill) * ease(seg(0.35f, 1.15f));
        else if (m_anim < 2.15f) {
            fill = m_from.fill + ((m_up ? 1.0f : 0.0f) - m_from.fill) * ease(seg(0.35f, 1.15f));
            edge = m_anim >= 1.15f;labelsAlpha = 1.0f - seg(1.95f, 2.15f);
        } else {
            fill = (m_up ? 0.0f : 1.0f) + (fill - (m_up ? 0.0f : 1.0f)) * ease(seg(2.15f, 2.95f));
            labelsAlpha = seg(2.15f, 2.35f);
        }
    }
    if (m_labels) propagateModeOpacity(m_labels, static_cast<GLubyte>(std::lround(255.0f * labelsAlpha * k)));
    if (leader) {
        // #1: the whole bar in the Mythic achievement's moving pastel rainbow.
        auto const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        constexpr int segments = 42;
        for (int i = 0; i < segments; ++i) {
            auto const a = left + m_width * static_cast<float>(i) / segments, b = left + m_width * static_cast<float>(i + 1) / segments;
            quad(m_draw, a, y0, b + 0.3f, y1, mythicPastel(static_cast<float>(i) / segments, seconds, k));
        }
    } else if (fill > 0.0f) {
        auto const pulse = 0.5f + 0.5f * std::sin(m_anim * 18.0f);
        auto const color = edge ? (m_up ? Gold : (pulse > 0.5f ? Red : ccColor3B{122, 34, 48})) : m_view.kind == TierProgressKind::Warning ? Red
            : m_view.kind == TierProgressKind::Promotion ? Gold : m_view.kind == TierProgressKind::Near ? Green : m_tone;
        auto const glow = m_view.kind == TierProgressKind::Promotion ? 0.75f + 0.25f * std::sin(m_time * 4.0f) : 1.0f;
        auto const x = left + m_width * std::max(fill, 0.02f);
        quad(m_draw, left, y0, x, y1, rgba(color, glow * k));
        quad(m_draw, left, y0, x, y0 + 1.6f, {0, 0, 0, 0.28f * k});
        if (fill < 1.0f) quad(m_draw, x - 1.0f, y0 - 2.0f, x + 1.0f, y1 + 2.0f, {0.95f, 0.97f, 1.0f, k});
    }
    // Quarter ticks.
    for (int i = 1; i < 4; ++i) {
        auto const x = left + m_width * static_cast<float>(i) / 4.0f;
        quad(m_draw, x - 0.4f, y0, x + 0.4f, y1, {0.95f, 0.97f, 1.0f, 0.16f * k});
    }
}

} // namespace corum::ranked::ui
