#include "TierProgressBar.hpp"
#include "PlayerIcon.hpp"
#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"
#include "RankedTranslations.hpp"
#include "ModeLobbyMotion.hpp"
#include "../domain/RankTierPresentation.hpp"
#include "../domain/AchievementStyle.hpp"
#include "../domain/LeaderboardPresentation.hpp"
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

TierProgressBar* TierProgressBar::create(TierProgressView view, float width, std::string const& promotionTarget, bool compact) {
    auto* node = new TierProgressBar;
    if (node->init(std::move(view), width, promotionTarget, compact)) { node->autorelease();return node; }
    delete node;return nullptr;
}

bool TierProgressBar::init(TierProgressView view, float width, std::string const& promotionTarget, bool compact) {
    if (!CCNode::init()) return false;
    setID("ranked-tier-progress"_spr);
    m_view = std::move(view);m_width = width;m_compact = compact;
    m_tone = tierColor(m_view.tier);
    m_labels = buildLabels(m_view, promotionTarget);addChild(m_labels, 3);
    // Receives the panel fade only (the labels may be dimmed by the bar itself).
    m_probe = CCNodeRGBA::create();m_probe->setCascadeOpacityEnabled(true);addChild(m_probe);
    m_draw = CCDrawNode::create();
    m_draw->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_draw, 1);
    redraw();
    if (m_view.kind == TierProgressKind::Leader || m_view.kind == TierProgressKind::Promotion || m_view.style != TierBarStyle::Plain) scheduleUpdate();
    else schedule(schedule_selector(TierProgressBar::update), 0.1f);
    return true;
}

// Badges, names, caption and score of one state of the bar.
CCNode* TierProgressBar::buildLabels(TierProgressView const& view, std::string const& promotionTarget) {
    auto* root = CCNode::create();
    auto const width = m_width;
    auto const left = -width / 2.0f, right = width / 2.0f;
    auto const tier = view.tier;
    auto const tone = tierColor(tier);
    // Top row: own badge and tier, then the next tier (or rank) on the right
    // (left out when compact: the mode card already shows the emblem and tier).
    auto const row = m_compact ? -11.0f : -14.0f;
    if (!m_compact) {
    auto* badge = createAnimatedRankBadge(tier, {18, 18}, RankBadgeDisplay::Matched);
    badge->setPosition({left + 9.0f, 11.0f});root->addChild(badge, 3);
    auto* name = label(rankTierDisplay(tier), .25f, {left + 21.0f, 11.0f}, tone, FontWeight::Black);
    name->setAnchorPoint({0.0f, 0.5f});fitMenuLabelWidth(name, width * .48f, .25f, .15f);root->addChild(name, 4);
    std::string next;CCNode* nextBadge = nullptr;
    switch (view.kind) {
        case TierProgressKind::Progress: case TierProgressKind::Near: case TierProgressKind::Warning:
            next = rankTierDisplay(view.nextTier);nextBadge = createAnimatedRankBadge(view.nextTier, {15, 15}, RankBadgeDisplay::Matched);break;
        case TierProgressKind::Promotion:
            if (!promotionTarget.empty()) { next = rankTierDisplay(promotionTarget);nextBadge = createAnimatedRankBadge(promotionTarget, {15, 15}, RankBadgeDisplay::Matched); }
            break;
        case TierProgressKind::RankChase: nextBadge = rankChip(view.targetRank);break;
        case TierProgressKind::Leader: case TierProgressKind::Standing: {
            // The own icon beside the own ladder rank.
            auto* holder = CCNode::create();
            holder->addChild(rankChip(view.kind == TierProgressKind::Leader ? 1 : view.targetRank), 2);
            if (auto* icon = PlayerIcon::createLocal()) {
                auto const box = icon->getContentSize();
                icon->setScale(14.0f / std::max(1.0f, std::max(box.width, box.height)));
                icon->setPosition({-18.0f, 0.0f});holder->addChild(icon, 1);
            }
            nextBadge = holder;break;
        }
        default: break;
    }
    auto nextX = right;
    if (nextBadge) { nextBadge->setPosition({right - 9.0f, 11.0f});root->addChild(nextBadge, 3);
        nextX = right - (view.kind == TierProgressKind::Leader || view.kind == TierProgressKind::Standing ? 38.0f : 20.0f); }
    if (!next.empty()) {
        auto* nextLabel = label(next, .20f, {nextX, 11.0f}, TextMuted, FontWeight::ExtraBold);
        nextLabel->setAnchorPoint({1.0f, 0.5f});fitMenuLabelWidth(nextLabel, width * .34f, .20f, .12f);root->addChild(nextLabel, 4);
    }
    }
    // Bottom row: what is left, and the score (or the series marks).
    std::string caption, score;ccColor3B captionColor = Gold;
    switch (view.kind) {
        case TierProgressKind::Progress:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-next", "{} LP to next tier")), view.remaining);break;
        case TierProgressKind::Near:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-near", "Almost there: {} LP")), view.remaining);captionColor = Green;break;
        case TierProgressKind::Warning:
            caption = i18n::text("demotion-warning", "DEMOTION WARNING");captionColor = Red;break;
        case TierProgressKind::Promotion:
            caption = i18n::text("tier-bar-promotion", "PROMOTION SERIES");break;
        case TierProgressKind::RankChase:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-rank", "To #{}: {} LP")), view.targetRank, view.remaining);break;
        case TierProgressKind::Leader:
            caption = i18n::text("tier-bar-leader", "#1 ON THE LADDER");break;
        case TierProgressKind::Standing:
            caption = fmt::format(fmt::runtime(i18n::text("tier-bar-standing", "#{} ON THE LADDER")), view.targetRank);break;
        case TierProgressKind::Top:
            caption = i18n::text("tier-bar-top", "TOP TIER");break;
        default: break;
    }
    if (view.kind == TierProgressKind::Progress || view.kind == TierProgressKind::Near || view.kind == TierProgressKind::Warning
        || view.kind == TierProgressKind::RankChase)
        score = fmt::format("{} / {} LP", view.score, view.maximum);
    else if (view.kind != TierProgressKind::Promotion) score = fmt::format("{} LP", view.score);
    auto* captionLabel = label(caption, .21f, {left, row}, captionColor, FontWeight::ExtraBold);
    captionLabel->setAnchorPoint({0.0f, 0.5f});fitMenuLabelWidth(captionLabel, width * .62f, .21f, .13f);root->addChild(captionLabel, 4);
    if (!m_caption) m_caption = captionLabel;
    if (!score.empty()) {
        auto* scoreLabel = label(score, .18f, {right, row}, TextMuted, FontWeight::SemiBold);
        scoreLabel->setAnchorPoint({1.0f, 0.5f});fitMenuLabelWidth(scoreLabel, width * .36f, .18f, .11f);root->addChild(scoreLabel, 4);
        if (!m_score) m_score = scoreLabel;
    }
    if (view.kind == TierProgressKind::Promotion) {
        // Series marks, right-aligned: won (green), lost (red), left (empty).
        auto const games = std::clamp(view.promotionGames, 1, 9);
        for (int i = 0; i < games; ++i) {
            auto const x = right - 4.0f - static_cast<float>(games - 1 - i) * 10.0f;
            auto const result = i < static_cast<int>(view.promotionResults.size()) ? view.promotionResults[static_cast<std::size_t>(i)] : std::string();
            auto const accent = result == "WIN" ? Green : result.empty() ? TextMuted : Red;
            auto* mark = surface({8, 8}, {x, row}, accent, result.empty() ? Ink : SurfaceSoft, 255, !result.empty());
            root->addChild(mark, 4);
        }
    }
    return root;
}

void TierProgressBar::countScoreFrom(int score) {
    if (!m_score || score == m_view.score) return;
    m_scoreFrom = score;m_scoreAnim = 0.0f;
    m_score->setString(fmt::format("{} LP", score).c_str());
    unschedule(schedule_selector(TierProgressBar::update));scheduleUpdate();
}

void TierProgressBar::update(float dt) {
    m_time += dt;
    if (m_scoreAnim >= 0.0f && m_score) {
        m_scoreAnim += dt;
        auto const t = std::clamp((m_scoreAnim - 0.2f) / 0.9f, 0.0f, 1.0f), eased = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
        auto const value = static_cast<int>(std::lround(m_scoreFrom + (m_view.score - m_scoreFrom) * eased));
        m_score->setString(fmt::format("{} LP", value).c_str());
        if (t >= 1.0f) m_scoreAnim = -1.0f;
    }
    if (m_animating) {
        m_anim += dt;
        if (m_anim > 3.2f) {
            m_animating = false;
            // The previous tier's labels leave the bar for good, so a later
            // lobby fade cannot bring them back.
            if (m_oldLabels) { m_oldLabels->removeFromParentAndCleanup(true);m_oldLabels = nullptr; }
        }
    }
    redraw();
}

// After an LP change: the bar runs from the previous value. Across a tier
// change it first runs to the old band's edge (full gold / empty red) still
// showing the old tier, then hands over once: the old labels fade out, the
// bar restarts in the new band and the new labels fade in.
void TierProgressBar::animateFrom(TierProgressView from, bool up) {
    m_from = std::move(from);m_up = up;m_anim = 0.0f;m_animating = true;
    m_tierChange = m_from.tier != m_view.tier;
    if (m_oldLabels) { m_oldLabels->removeFromParentAndCleanup(true);m_oldLabels = nullptr; }
    if (m_tierChange) {
        m_fromTone = tierColor(m_from.tier);
        m_oldLabels = buildLabels(m_from, {});addChild(m_oldLabels, 3);
    }
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
    auto fill = std::clamp(m_view.fill, 0.0f, 1.0f);
    auto edge = false;
    auto labelsAlpha = 1.0f, oldAlpha = 0.0f;
    // Before the hand-over the bar still belongs to the previous tier.
    auto const before = m_animating && m_tierChange && m_anim < 2.15f;
    auto const& shown = before ? m_from : m_view;
    auto const tone = before ? m_fromTone : m_tone;
    auto const leader = shown.kind == TierProgressKind::Leader;
    if (m_animating) {
        auto const seg = [&](float a, float b) { return std::clamp((m_anim - a) / (b - a), 0.0f, 1.0f); };
        auto const ease = [](float x) { return x < 0.5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.0f) / 2; };
        if (!m_tierChange) fill = m_from.fill + (fill - m_from.fill) * ease(seg(0.35f, 1.15f));
        else if (m_anim < 2.15f) {
            fill = m_from.fill + ((m_up ? 1.0f : 0.0f) - m_from.fill) * ease(seg(0.35f, 1.15f));
            edge = m_anim >= 1.15f;labelsAlpha = 0.0f;oldAlpha = 1.0f - seg(1.95f, 2.15f);
        } else {
            fill = (m_up ? 0.0f : 1.0f) + (fill - (m_up ? 0.0f : 1.0f)) * ease(seg(2.15f, 2.95f));
            labelsAlpha = seg(2.15f, 2.35f);
        }
    }
    if (m_labels) propagateModeOpacity(m_labels, static_cast<GLubyte>(std::lround(255.0f * labelsAlpha * k)));
    if (m_oldLabels) propagateModeOpacity(m_oldLabels, static_cast<GLubyte>(std::lround(255.0f * oldAlpha * k)));
    auto const edgeColor = m_up ? Gold : Red;
    auto const border = edge ? edgeColor : shown.kind == TierProgressKind::Warning ? Red
        : shown.kind == TierProgressKind::Promotion || shown.kind == TierProgressKind::RankChase ? Gold : tone;
    // Frame, track. GOLD ladder bars get a thin frame instead: gold, or the
    // Mythic diamond pastels for the elite (Survival Top 3, Solo CHAMPION).
    auto const seconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto const style = edge || shown.kind == TierProgressKind::Warning ? TierBarStyle::Plain : shown.style;
    if (style == TierBarStyle::Plain) {
        quad(m_draw, left - 1.5f, y0 - 1.5f, right + 1.5f, y1 + 1.5f, rgba(border, (leader ? 0.9f : 0.55f) * k));
    } else {
        constexpr float frameWidth = 1.0f;
        auto const fx0 = left - frameWidth, fy0 = y0 - frameWidth, fx1 = right + frameWidth, fy1 = y1 + frameWidth;
        if (style == TierBarStyle::Elite) {
            // The frame in the flowing Mythic pastels, segment by segment.
            constexpr int pieces = 36;
            for (int i = 0; i < pieces; ++i) {
                auto const a = fx0 + (fx1 - fx0) * static_cast<float>(i) / pieces, b = fx0 + (fx1 - fx0) * static_cast<float>(i + 1) / pieces;
                auto const color = mythicPastel(static_cast<float>(i) / pieces, seconds, 0.95f * k);
                quad(m_draw, a, fy1 - frameWidth, b + 0.2f, fy1, color);
                quad(m_draw, a, fy0, b + 0.2f, fy0 + frameWidth, mythicPastel(1.0f - static_cast<float>(i) / pieces, seconds, 0.95f * k));
            }
            quad(m_draw, fx0, fy0, fx0 + frameWidth, fy1, mythicPastel(0.0f, seconds, 0.95f * k));
            quad(m_draw, fx1 - frameWidth, fy0, fx1, fy1, mythicPastel(1.0f, seconds, 0.95f * k));
        } else {
            quad(m_draw, fx0, fy0, fx1, fy1, rgba(Gold, 0.9f * k));
        }
        // A short gloss runs clockwise around the frame every 3 s (the
        // achievement card shine).
        auto const stroke = clockwiseBorderStroke(fx1 - fx0, fy1 - fy0, m_time / 3.0, (fx1 - fx0) * 0.18f);
        for (std::size_t index = 0; index < stroke.count; ++index) {
            auto const& segment = stroke.segments[index];
            auto const ax = fx0 + std::min(segment.from.x, segment.to.x), bx = fx0 + std::max(segment.from.x, segment.to.x);
            auto const ay = fy0 + std::min(segment.from.y, segment.to.y), by = fy0 + std::max(segment.from.y, segment.to.y);
            quad(m_draw, ax - 0.9f, ay - 0.9f, bx + 0.9f, by + 0.9f, {1, 1, 1, 0.85f * k});
        }
    }
    quad(m_draw, left, y0, right, y1, {10 / 255.0f, 19 / 255.0f, 36 / 255.0f, k});
    if (leader) {
        // #1: the whole bar in the Mythic achievement's moving pastel rainbow.
        constexpr int segments = 42;
        for (int i = 0; i < segments; ++i) {
            auto const a = left + m_width * static_cast<float>(i) / segments, b = left + m_width * static_cast<float>(i + 1) / segments;
            quad(m_draw, a, y0, b + 0.3f, y1, mythicPastel(static_cast<float>(i) / segments, seconds, k));
        }
    } else if (fill > 0.0f) {
        auto const pulse = 0.5f + 0.5f * std::sin(m_anim * 18.0f);
        auto const color = edge ? (m_up ? Gold : (pulse > 0.5f ? Red : ccColor3B{122, 34, 48})) : shown.kind == TierProgressKind::Warning ? Red
            : shown.kind == TierProgressKind::Promotion || style != TierBarStyle::Plain ? Gold : shown.kind == TierProgressKind::Near ? Green : tone;
        auto const glow = shown.kind == TierProgressKind::Promotion ? 0.75f + 0.25f * std::sin(m_time * 4.0f) : 1.0f;
        auto const x = left + m_width * std::max(fill, 0.02f);
        quad(m_draw, left, y0, x, y1, rgba(color, glow * k));
        quad(m_draw, left, y0, x, y0 + 1.6f, {0, 0, 0, 0.28f * k});
        if (style != TierBarStyle::Plain) {
            // Gold fill shine: a soft light band sweeps across the filled part
            // every 2.4 s, like the achievement progress bars.
            auto const sweep = static_cast<float>(std::fmod(m_time / 2.4, 1.0));
            auto const band = 14.0f, cx = left + (x - left + band * 2.0f) * sweep - band;
            for (int i = 0; i < 6; ++i) {
                auto const a = cx + band * (static_cast<float>(i) / 6.0f - 0.5f), b = a + band / 6.0f;
                auto const alpha = 0.45f * (1.0f - std::abs(static_cast<float>(i) - 2.5f) / 3.0f);
                auto const ca = std::max(a, left), cb = std::min(b, x);
                if (cb > ca) quad(m_draw, ca, y0, cb, y1, {1, 1, 0.92f, alpha * k});
            }
        }
        if (fill < 1.0f) quad(m_draw, x - 1.0f, y0 - 2.0f, x + 1.0f, y1 + 2.0f, {0.95f, 0.97f, 1.0f, k});
    }
    // Quarter ticks.
    for (int i = 1; i < 4; ++i) {
        auto const x = left + m_width * static_cast<float>(i) / 4.0f;
        quad(m_draw, x - 0.4f, y0, x + 0.4f, y1, {0.95f, 0.97f, 1.0f, 0.16f * k});
    }
}

} // namespace corum::ranked::ui
