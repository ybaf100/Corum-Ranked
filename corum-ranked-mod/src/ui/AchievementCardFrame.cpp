#include "AchievementCardFrame.hpp"

#include "../domain/AchievementStyle.hpp"
#include "../domain/LeaderboardPresentation.hpp"
#include "AchievementEffects.hpp"
#include "RankedVisuals.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
ccColor4F rgba(ccColor3B color, float alpha) { return {color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, alpha}; }

void rect(CCDrawNode* node, float x0, float y0, float x1, float y1, ccColor4F color) {
    if (x1 <= x0 || y1 <= y0) return;
    CCPoint points[4] = {CCPoint{x0, y0}, CCPoint{x1, y0}, CCPoint{x1, y1}, CCPoint{x0, y1}};
    node->drawPolygon(points, 4, color, 0, color);
}

void diamond(CCDrawNode* node, CCPoint center, float radius, ccColor4F fill, ccColor4F edge) {
    CCPoint points[4] = {CCPoint{center.x, center.y + radius}, CCPoint{center.x + radius, center.y},
        CCPoint{center.x, center.y - radius}, CCPoint{center.x - radius, center.y}};
    node->drawPolygon(points, 4, fill, 0.6f, edge);
}

// Outline of a rectangle as four bars, `thickness` thick, inset by `inset`.
void frame(CCDrawNode* node, CCSize size, float inset, float thickness, ccColor4F color) {
    auto const x0 = inset, y0 = inset, x1 = size.width - inset, y1 = size.height - inset;
    rect(node, x0, y1 - thickness, x1, y1, color);
    rect(node, x0, y0, x1, y0 + thickness, color);
    rect(node, x0, y0 + thickness, x0 + thickness, y1 - thickness, color);
    rect(node, x1 - thickness, y0 + thickness, x1, y1 - thickness, color);
}

ccColor3B mix(ccColor3B a, ccColor3B b, float t) {
    auto const lerp = [t](GLubyte x, GLubyte y) { return static_cast<GLubyte>(std::lround(x + (y - x) * t)); };
    return {lerp(a.r, b.r), lerp(a.g, b.g), lerp(a.b, b.b)};
}

ccColor3B panelFor(int tier, bool achieved) {
    if (!achieved) return {16, 12, 28};
    switch (tier) {
        case 1: return {21, 25, 38};
        case 2: return {14, 26, 44};
        case 3: return {26, 16, 48};
        case 4: return {33, 22, 10};
        case 5: return {13, 31, 38};
        default: return {16, 12, 28};
    }
}
} // namespace

AchievementCardFrame* AchievementCardFrame::create(CCSize size, int tier, bool achieved) {
    auto* node = new AchievementCardFrame;
    if (node->init(size, tier, achieved)) { node->autorelease(); return node; }
    delete node;
    return nullptr;
}

bool AchievementCardFrame::init(CCSize size, int tier, bool achieved) {
    if (!CCNode::init()) return false;
    setID("ranked-achievement-card-frame"_spr);
    m_size = size;
    m_tier = std::clamp(tier, 0, 5);
    m_achieved = achieved;
    setContentSize(size);
    setAnchorPoint({0.5f, 0.5f});
    // A card not yet achieved keeps its rarity hue, dimmed toward the panel.
    m_tone = m_achieved ? achievementColor(m_tier) : mix(achievementColor(m_tier), {74, 69, 96}, 0.62f);
    m_static = CCDrawNode::create();
    m_static->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    addChild(m_static, 0);
    if (m_achieved && m_tier >= 4) {
        m_shine = CCDrawNode::create();
        m_shine->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        addChild(m_shine, 1);
        // Cards start at different points of the lap.
        m_time = static_cast<double>((reinterpret_cast<std::uintptr_t>(this) >> 4) % 97) / 97.0 * 5.0;
        scheduleUpdate();
    }
    drawStatic();
    drawShine();
    return true;
}

void AchievementCardFrame::setGems(CCPoint right, int filled, float size) {
    m_gemsRight = right;
    m_gemsFilled = std::clamp(filled, 0, 5);
    m_gemSize = std::max(0.0f, size);
    drawStatic();
}

void AchievementCardFrame::drawStatic() {
    if (!m_static) return;
    m_static->clear();
    auto const w = m_size.width, h = m_size.height;
    rect(m_static, 0, 0, w, h, rgba(panelFor(m_tier, m_achieved), 0.97f));
    if (m_achieved) {
        // A faint diagonal shine band, kept inside the card.
        auto const slant = (h - 4) * 0.45f, x0 = std::min(w * 0.46f, w - 30), band = std::max(8.0f, h * 0.18f);
        CCPoint shine[4] = {CCPoint{x0, h - 2}, CCPoint{x0 + band, h - 2}, CCPoint{x0 + band - slant, 2}, CCPoint{x0 - slant, 2}};
        m_static->drawPolygon(shine, 4, {1, 1, 1, 0.055f}, 0, {1, 1, 1, 0});
    }
    // Double frame: a strong outer border and a thin inner line.
    frame(m_static, m_size, 0, 1.5f, rgba(m_tone, m_achieved ? 0.95f : 0.55f));
    frame(m_static, m_size, 4.0f, 0.6f, rgba(m_tone, m_achieved ? 0.45f : 0.25f));
    // Corner gems.
    diamond(m_static, {5.0f, h - 5.0f}, 2.8f, rgba(m_tone, 1), rgba(m_tone, 1));
    diamond(m_static, {w - 5.0f, 5.0f}, 2.8f, rgba(m_tone, 1), rgba(m_tone, 1));
    // Rarity gems: five diamonds, the first `filled` solid.
    if (m_gemSize > 0) {
        for (int i = 0; i < 5; ++i) {
            auto const center = CCPoint{m_gemsRight.x - m_gemSize - (4 - i) * (m_gemSize * 2 + 1.6f), m_gemsRight.y};
            diamond(m_static, center, m_gemSize, i < m_gemsFilled ? rgba(m_tone, 1) : ccColor4F{0, 0, 0, 0}, rgba(m_tone, 0.9f));
        }
    }
}

// Two short streaks, half a lap apart, run clockwise around the border once
// every five seconds: gold for tier 4, the moving pastel rainbow for tier 5.
void AchievementCardFrame::drawShine() {
    if (!m_shine) return;
    m_shine->clear();
    auto const phase = m_time / 5.0;
    auto const perimeter = 2.0f * (m_size.width + m_size.height);
    auto const length = perimeter * 0.13f;
    auto const bar = [&](BorderSegment const& segment, float thickness, ccColor4F color) {
        auto const x0 = std::min(segment.from.x, segment.to.x), x1 = std::max(segment.from.x, segment.to.x);
        auto const y0 = std::min(segment.from.y, segment.to.y), y1 = std::max(segment.from.y, segment.to.y);
        auto const half = thickness / 2.0f;
        rect(m_shine, x0 - half, y0 - half, x1 + half, y1 + half, color);
    };
    for (auto const offset : {0.0, 0.5}) {
        auto const at = phase + offset;
        ccColor3B color = {255, 207, 74};
        if (m_tier >= 5) {
            auto const pastel = achievementPastelAt(at - std::floor(at), m_time);
            color = {static_cast<GLubyte>(pastel.r), static_cast<GLubyte>(pastel.g), static_cast<GLubyte>(pastel.b)};
        }
        auto const stroke = clockwiseBorderStroke(m_size.width - 1.5f, m_size.height - 1.5f, at, length);
        for (std::size_t index = 0; index < stroke.count; ++index) {
            auto segment = stroke.segments[index];
            segment.from.x += 0.75f; segment.from.y += 0.75f; segment.to.x += 0.75f; segment.to.y += 0.75f;
            bar(segment, 3.6f, rgba(color, 0.22f));
            bar(segment, 1.6f, rgba(color, 0.85f));
            bar(segment, 0.6f, {1, 1, 1, 0.95f});
        }
    }
}

void AchievementCardFrame::update(float dt) {
    m_time += std::max(0.0f, dt);
    drawShine();
}

void addAchievementInnerFrame(CCNode* node, CCSize size, ccColor3B color, float inset, GLubyte alpha) {
    if (!node) return;
    auto const line = [&](float x, float y, float w, float h) {
        auto* layer = CCLayerColor::create({color.r, color.g, color.b, alpha}, w, h);
        layer->setPosition({x, y});
        node->addChild(layer, 4);
    };
    auto const w = size.width - 2 * inset, h = size.height - 2 * inset;
    if (w <= 1 || h <= 1) return;
    line(inset, inset, w, 0.6f);
    line(inset, size.height - inset - 0.6f, w, 0.6f);
    line(inset, inset, 0.6f, h);
    line(size.width - inset - 0.6f, inset, 0.6f, h);
}

CCNode* achievementPlate(std::string const& text, CCSize size, ccColor3B accent, ccColor3B fill, ccColor3B ink, float scale) {
    auto* plate = surface(size, {}, accent, fill, 250, false);
    addAchievementInnerFrame(plate, size, accent, 3.0f, 120);
    auto* caption = menuLabel(text, scale, {size.width / 2, size.height / 2}, ink, FontWeight::Black);
    fitMenuLabelWidth(caption, size.width - 14, scale, scale * 0.6f);
    plate->addChild(caption, 6);
    return plate;
}

CCNode* achievementTabFace(std::string const& text, CCSize size, bool selected) {
    auto* root = CCNode::create();
    root->setContentSize(size);
    root->setAnchorPoint({0.5f, 0.5f});
    auto* shape = CCDrawNode::create();
    shape->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
    root->addChild(shape, 0);
    // Pointed ends: a flat hexagon.
    auto const tip = std::min(7.0f, size.height / 2);
    CCPoint points[6] = {CCPoint{tip, 0}, CCPoint{size.width - tip, 0}, CCPoint{size.width, size.height / 2},
        CCPoint{size.width - tip, size.height}, CCPoint{tip, size.height}, CCPoint{0, size.height / 2}};
    auto const fill = selected ? ccColor3B{44, 30, 82} : ccColor3B{20, 13, 40};
    auto const edge = selected ? ccColor3B{232, 217, 255} : ccColor3B{74, 58, 114};
    shape->drawPolygon(points, 6, rgba(fill, 1), 1.0f, rgba(edge, 1));
    auto* caption = menuLabel(text, 0.30f, {size.width / 2, size.height / 2}, selected ? ccColor3B{255, 255, 255} : ccColor3B{168, 147, 214},
        selected ? FontWeight::Black : FontWeight::ExtraBold);
    fitMenuLabelWidth(caption, size.width - 2 * tip - 6, 0.30f, 0.18f);
    root->addChild(caption, 5);
    return root;
}

CCNode* achievementButtonFace(std::string const& text, CCSize size, AchievementButtonStyle style, float scale) {
    ccColor3B border{46, 36, 72}, fill{18, 12, 36}, ink{110, 98, 144};
    switch (style) {
        case AchievementButtonStyle::Claim: border = {255, 227, 168}; fill = {255, 176, 70}; ink = {42, 26, 4}; break;
        case AchievementButtonStyle::Title: border = {169, 124, 255}; ink = {232, 217, 255}; break;
        case AchievementButtonStyle::Current: border = {232, 217, 255}; fill = {44, 30, 82}; ink = {255, 255, 255}; break;
        case AchievementButtonStyle::Nav: border = {169, 124, 255}; fill = {28, 18, 54}; ink = {232, 217, 255}; break;
        case AchievementButtonStyle::Disabled: border = {46, 36, 72}; ink = {74, 58, 114}; break;
        case AchievementButtonStyle::Muted: break;
    }
    auto* face = surface(size, {}, border, fill, 255, false);
    auto* caption = menuLabel(text, scale, {size.width / 2, size.height / 2}, ink, FontWeight::Black);
    fitMenuLabelWidth(caption, size.width - 8, scale, scale * 0.55f);
    face->addChild(caption, 6);
    return face;
}

} // namespace corum::ranked::ui
