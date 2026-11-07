#include "LobbyScoreTransition.hpp"
#include "ModeLobbyMotion.hpp"
#include "RankedVisuals.hpp"
#include <Geode/cocos/misc_nodes/CCClippingNode.h>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;
namespace corum::ranked::ui {
namespace {
float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
float inOut(float x) { return x < 0.5f ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.0f) / 2; }
float outCubic(float x) { return 1 - std::pow(1 - x, 3.0f); }
float outBack(float x) { constexpr float c1 = 1.4f, c3 = c1 + 1; return 1 + c3 * std::pow(x - 1, 3.0f) + c1 * std::pow(x - 1, 2.0f); }
constexpr float Stagger = 0.09f, Spin = 0.9f;
}

RollingNumber* RollingNumber::create(int from, int to, std::string const& suffix, float scale, ccColor3B color) {
    auto* node = new RollingNumber;
    if (node->init(from, to, suffix, scale, color)) { node->autorelease();return node; }
    delete node;return nullptr;
}

bool RollingNumber::init(int from, int to, std::string const& suffix, float scale, ccColor3B color) {
    if (!CCNode::init()) return false;
    m_color = color;m_roll = to >= from ? ccColor3B{255, 242, 184} : ccColor3B{255, 194, 202};
    auto const fromText = std::to_string(std::max(0, from)), toText = std::to_string(std::max(0, to));
    auto const count = std::max(fromText.size(), toText.size());
    auto const padded = [&](std::string const& text) { return std::string(count - text.size(), '0') + text; };
    auto const a = padded(fromText), b = padded(toText);
    auto* probe = menuLabel("8", scale, {}, color, FontWeight::Black);
    auto const digitW = probe->getScaledContentSize().width * 0.92f;
    m_height = probe->getScaledContentSize().height * 0.86f;
    auto* lp = menuLabel(suffix, scale, {}, color, FontWeight::Black);
    auto const gap = suffix.empty() ? 0.0f : digitW * 0.35f;
    auto const total = digitW * static_cast<float>(count) + gap + lp->getScaledContentSize().width;
    auto const left = -total / 2.0f;
    // One clipping window over all digit columns.
    auto* mask = CCDrawNode::create();
    CCPoint rect[] {{left - 1, -m_height / 2}, {left + digitW * count + 1, -m_height / 2}, {left + digitW * count + 1, m_height / 2}, {left - 1, m_height / 2}};
    mask->drawPolygon(rect, 4, {1, 1, 1, 1}, 0, {1, 1, 1, 1});
    auto* clip = CCClippingNode::create(mask);addChild(clip, 1);
    auto const up = to >= from;
    for (std::size_t i = 0; i < count; ++i) {
        Column column;
        auto const fromDigit = static_cast<float>(a[i] - '0'), toDigit = static_cast<float>(b[i] - '0');
        column.changed = a[i] != b[i];
        // Index space 0..29: start on the middle copy, spin a full turn either way.
        column.from = 10 + fromDigit;
        column.to = !column.changed ? 10 + toDigit : up ? 20 + toDigit : toDigit;
        column.hideAtEnd = i + toText.size() < count; // leading zero of the final number
        column.a = menuLabel("0", scale, {left + digitW * (static_cast<float>(i) + 0.5f), 0}, color, FontWeight::Black);
        column.b = menuLabel("0", scale, {left + digitW * (static_cast<float>(i) + 0.5f), 0}, color, FontWeight::Black);
        clip->addChild(column.a);clip->addChild(column.b);
        m_columns.push_back(column);
    }
    lp->setPosition({left + digitW * count + gap + lp->getScaledContentSize().width / 2.0f, 0});addChild(lp, 2);
    setContentSize({total, m_height});
    update(0.0f);
    scheduleUpdate();
    return true;
}

void RollingNumber::update(float dt) {
    if (m_done) return;
    m_time += dt;
    auto finished = true;
    for (std::size_t i = 0; i < m_columns.size(); ++i) {
        auto& column = m_columns[i];
        auto const start = m_delay + Stagger * static_cast<float>(m_columns.size() - 1 - i);
        auto const p = column.changed ? outCubic(seg(m_time, start, start + Spin)) : 1.0f;
        if (p < 1.0f) finished = false;
        auto const v = column.from + (column.to - column.from) * p;
        auto const base = std::floor(v);auto const frac = v - base;
        auto const digit = (static_cast<int>(base) % 10 + 10) % 10;
        column.a->setString(std::to_string(digit).c_str());column.b->setString(std::to_string((digit + 1) % 10).c_str());
        auto const x = column.a->getPositionX();
        column.a->setPosition({x, frac * m_height});column.b->setPosition({x, frac * m_height - m_height});
        auto const rolling = column.changed && p > 0.0f && p < 1.0f;
        column.a->setColor(rolling ? m_roll : m_color);column.b->setColor(rolling ? m_roll : m_color);
        auto const hidden = column.hideAtEnd && p >= 1.0f;
        column.a->setVisible(!hidden);column.b->setVisible(!hidden && frac > 0.001f);
    }
    if (finished && m_time > m_delay) { m_done = true;unscheduleUpdate(); }
}

LobbyTierTransition* LobbyTierTransition::create(CCLayerColor* group, CCNode* oldEmblem, CCNode* newEmblem, CCNode* oldName,
    CCNode* newName, CCNode* bloom, bool up) {
    auto* node = new LobbyTierTransition;
    if (node->init(group, oldEmblem, newEmblem, oldName, newName, bloom, up)) { node->autorelease();return node; }
    delete node;return nullptr;
}

bool LobbyTierTransition::init(CCLayerColor* group, CCNode* oldEmblem, CCNode* newEmblem, CCNode* oldName, CCNode* newName, CCNode* bloom, bool up) {
    if (!CCNode::init() || !oldEmblem || !newEmblem || !oldName || !newName) return false;
    m_group = group;m_oldEmblem = oldEmblem;m_newEmblem = newEmblem;m_oldName = oldName;m_newName = newName;m_bloom = bloom;m_up = up;
    m_emblemAt = newEmblem->getPosition();m_nameAt = newName->getPosition();
    apply();scheduleUpdate();
    return true;
}

void LobbyTierTransition::update(float dt) {
    m_time += dt;
    apply();
    if (m_time > 2.6f) finish();
}

// The faded-out nodes must leave the card: the lobby re-applies the card's
// opacity to every child on its own fades, which would bring them back.
void LobbyTierTransition::finish() {
    unscheduleUpdate();
    for (auto* node : {m_oldEmblem.data(), m_oldName.data(), m_bloom.data()})
        if (node && node->getParent()) node->removeFromParentAndCleanup(true);
    m_oldEmblem = nullptr;m_oldName = nullptr;m_bloom = nullptr;
    auto const opacity = static_cast<GLubyte>(m_group ? m_group->getOpacity() : 255);
    if (m_newEmblem.data()) { m_newEmblem->setScale(1.0f);m_newEmblem->setPosition(m_emblemAt);propagateModeOpacity(m_newEmblem.data(), opacity); }
    if (m_newName.data()) { m_newName->setPosition(m_nameAt);propagateModeOpacity(m_newName.data(), opacity); }
    auto const keep = Ref<LobbyTierTransition>(this);
    queueInMainThread([keep] { if (keep->getParent()) keep->removeFromParentAndCleanup(true); });
}

void LobbyTierTransition::apply() {
    if (!m_oldEmblem.data() || !m_newEmblem.data() || !m_oldName.data() || !m_newName.data()) return;
    auto const t = m_time;
    auto const k = m_group ? static_cast<float>(m_group->getOpacity()) / 255.0f : 1.0f;
    auto const out = inOut(seg(t, 1.15f, 1.6f)), in = seg(t, 1.4f, 2.05f);
    auto const fade = [&](CCNode* node, float alpha) { if (node) propagateModeOpacity(node, static_cast<GLubyte>(std::lround(255.0f * clamp01(alpha) * k))); };
    // Emblems.
    m_oldEmblem->setScale(m_up ? 1.0f - 0.18f * out : 1.0f - 0.08f * out);
    m_oldEmblem->setPosition(m_emblemAt + CCPoint{0, m_up ? 4.0f * out : -9.0f * out});
    fade(m_oldEmblem.data(), 1.0f - out);
    m_newEmblem->setScale(m_up ? 1.22f - 0.22f * outBack(in) : 0.9f + 0.1f * in);
    m_newEmblem->setPosition(m_emblemAt + CCPoint{0, m_up ? 0.0f : 7.0f * (1.0f - in)});
    fade(m_newEmblem.data(), in);
    // Halo bloom behind the new emblem.
    if (m_bloom.data()) {
        auto const bloom = std::sin(3.14159265f * seg(t, 1.3f, 2.3f)) * (m_up ? 1.0f : 0.5f);
        m_bloom->setScale(0.7f + 0.55f * seg(t, 1.3f, 2.3f));
        fade(m_bloom.data(), bloom);
    }
    // Tier names: promotion rises, demotion sinks.
    auto const dir = m_up ? 1.0f : -1.0f;
    m_oldName->setPosition(m_nameAt + CCPoint{0, dir * 12.0f * inOut(seg(t, 1.3f, 1.7f))});
    fade(m_oldName.data(), 1.0f - seg(t, 1.3f, 1.7f));
    m_newName->setPosition(m_nameAt + CCPoint{0, -dir * 12.0f * (1.0f - inOut(seg(t, 1.5f, 1.9f)))});
    fade(m_newName.data(), seg(t, 1.5f, 1.9f));
}

} // namespace corum::ranked::ui
