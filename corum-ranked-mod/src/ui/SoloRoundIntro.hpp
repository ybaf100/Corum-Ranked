#pragma once
#include "RankedVisuals.hpp"
#include "ModeLobbyMotion.hpp"
#include "../domain/SoloRoundIntroPolicy.hpp"
#include <functional>
#include <string>
#include <vector>

namespace corum::ranked::ui {
using namespace geode::prelude;

// One round card of the overview (completed, pending or locked).
struct SoloIntroCard {
    std::string label, result, map, detail, you, opp;
    ccColor3B accent = TextMuted, resultColor = TextMuted, detailColor = TextMuted;
    bool locked = false;
};
// The card that revolves into the next map.
struct SoloIntroReveal {
    std::string label, chip, mapTitle, creator, detail, levelText;
    ccColor3B accent = Cyan, detailColor = Orange;
    CCNode* thumbnail = nullptr; // optional, already sized to kSoloIntroThumb
};
inline constexpr float kSoloCardW = 147.f, kSoloCardH = 182.f, kSoloSlot = 164.4f;
inline const CCSize kSoloIntroThumb{129.f, 46.f};

/** Round overview, death-match shift/slam and revolving map reveal. Pure
 * presentation: positions are a function of elapsed time, so a rebuilt node
 * resumes at the same frame. onBoard fires once when the board should rise. */
class SoloRoundIntro final : public CCLayerColor {
    struct Card { CCLayerColor* node = nullptr; int index = 0; };
    std::vector<Card> m_cards;
    CCLayerColor* m_reveal = nullptr;
    CCNode *m_front = nullptr, *m_back = nullptr, *m_flip = nullptr;
    CCLayerColor* m_ring = nullptr;
    SoloIntroTimeline m_time;
    bool m_shift = false, m_boardFired = false, m_doneFired = false;
    int m_revealSlot = 2;
    float m_elapsed = 0;
    std::function<void(int)> m_phase;
    std::function<void()> m_onBoard, m_onDone;
    int m_lastPhase = -1;

    static CCLabelBMFont* text(CCNode* parent, std::string const& value, float scale, CCPoint position, ccColor3B color,
        FontWeight weight, float width, CCPoint anchor = {0.5f, 0.5f}) {
        auto* label = menuLabel(value, scale, position, color, weight);
        label->setAnchorPoint(anchor);
        fitMenuLabelWidth(label, width, scale, scale * 0.55f);
        parent->addChild(label, 5);
        return label;
    }
    static CCNode* box(CCNode* parent, CCSize size, CCPoint center, ccColor3B accent, ccColor3B fill = Surface, int z = 2) {
        auto* node = surface(size, center, accent, fill, 246, false);
        parent->addChild(node, z);
        return node;
    }
    static CCLayerColor* roundCard(SoloIntroCard const& card) {
        auto* root = CCLayerColor::create({0, 0, 0, 255}, 0, 0);
        root->setCascadeOpacityEnabled(true);
        auto const w = kSoloCardW, h = kSoloCardH, left = -w / 2 + 9, top = h / 2;
        root->addChild(surface({w, h}, {}, card.accent, Ink, 246, true), 0);
        text(root, card.label, 0.33f, {left, top - 16}, Text, FontWeight::Black, 80, {0, 0.5f});
        if (!card.result.empty()) {
            box(root, {44, 15}, {w / 2 - 9 - 22, top - 16}, card.resultColor, Ink, 3);
            text(root, card.result, 0.20f, {w / 2 - 9 - 22, top - 16}, card.resultColor, FontWeight::Black, 40);
        }
        if (card.locked) {
            text(root, "-", 0.9f, {0, 0}, TextMuted, FontWeight::Black, 60);
            return root;
        }
        text(root, card.map, 0.29f, {left, top - 38}, Text, FontWeight::ExtraBold, w - 18, {0, 0.5f});
        text(root, card.detail, 0.19f, {left, top - 52}, card.detailColor, FontWeight::SemiBold, w - 18, {0, 0.5f});
        for (int i = 0; i < 2; ++i) {
            auto const y = top - 87 - i * 46.f;
            box(root, {w - 18, 41}, {0, y}, i == 0 ? Gold : TextMuted, Surface, 2);
            text(root, i == 0 ? "YOU" : "OPPONENT", 0.20f, {left + 7, y + 10}, i == 0 ? Gold : TextMuted, FontWeight::ExtraBold, 80, {0, 0.5f});
            text(root, i == 0 ? card.you : card.opp, 0.45f, {left + 7, y - 7}, i == 0 ? Text : ccc3(195, 205, 224), FontWeight::Black, w - 32, {0, 0.5f});
        }
        return root;
    }
    void buildReveal(SoloIntroReveal const& reveal) {
        m_reveal = CCLayerColor::create({0, 0, 0, 255}, 0, 0);
        m_reveal->setCascadeOpacityEnabled(true);
        addChild(m_reveal, 6);
        m_flip = CCNode::create(); m_reveal->addChild(m_flip, 1);
        auto const w = kSoloCardW, h = kSoloCardH, left = -w / 2 + 9, top = h / 2;
        m_front = CCNode::create(); m_flip->addChild(m_front, 1);
        m_front->addChild(surface({w, h}, {}, reveal.accent, Surface, 250, true), 0);
        text(m_front, reveal.label, 0.33f, {left, top - 16}, reveal.accent, FontWeight::Black, 90, {0, 0.5f});
        box(m_front, {58, 15}, {w / 2 - 9 - 29, top - 16}, reveal.accent, Ink, 3);
        text(m_front, reveal.chip, 0.19f, {w / 2 - 9 - 29, top - 16}, reveal.accent, FontWeight::Black, 54);
        text(m_front, "?", 2.0f, {0, 4}, reveal.accent, FontWeight::Black, 120);
        text(m_front, "NEXT MAP", 0.25f, {0, -h / 2 + 22}, TextMuted, FontWeight::ExtraBold, w - 20);
        m_back = CCNode::create(); m_flip->addChild(m_back, 2); m_back->setVisible(false);
        m_back->addChild(surface({w, h}, {}, reveal.detailColor, Ink, 250, true), 0);
        text(m_back, reveal.label, 0.33f, {left, top - 16}, Text, FontWeight::Black, 80, {0, 0.5f});
        text(m_back, reveal.levelText, 0.19f, {w / 2 - 9, top - 16}, TextMuted, FontWeight::SemiBold, 60, {1, 0.5f});
        auto y = top - 34.f;
        if (reveal.thumbnail) {
            box(m_back, {kSoloIntroThumb.width + 2, kSoloIntroThumb.height + 2}, {0, y - kSoloIntroThumb.height / 2}, Blue, Ink, 2);
            reveal.thumbnail->setPosition({0, y - kSoloIntroThumb.height / 2}); m_back->addChild(reveal.thumbnail, 3);
            y -= kSoloIntroThumb.height + 8;
        } else y -= 6;
        text(m_back, reveal.mapTitle, 0.36f, {left, y - 8}, Text, FontWeight::Black, w - 18, {0, 0.5f});
        text(m_back, "by " + reveal.creator, 0.21f, {left, y - 25}, TextMuted, FontWeight::SemiBold, w - 18, {0, 0.5f});
        box(m_back, {w - 18, 28}, {0, y - 48}, reveal.detailColor, Surface, 2);
        text(m_back, reveal.detail, 0.22f, {0, y - 48}, reveal.detailColor, FontWeight::Black, w - 30);
        box(m_back, {w - 18, 15}, {0, -h / 2 + 15}, Cyan, Ink, 2);
        text(m_back, "MAP DECIDED", 0.21f, {0, -h / 2 + 15}, Cyan, FontWeight::Black, w - 30);
        // Impact ring for the death-match slam: four thin edges around the card.
        m_ring = CCLayerColor::create({0, 0, 0, 0}, 0, 0); m_ring->setCascadeOpacityEnabled(true);
        for (auto const& [size, pos] : std::vector<std::pair<CCSize, CCPoint>>{
                {{w + 6, 2}, {-w / 2 - 3, h / 2 + 1}}, {{w + 6, 2}, {-w / 2 - 3, -h / 2 - 3}},
                {{2, h + 6}, {-w / 2 - 3, -h / 2 - 3}}, {{2, h + 6}, {w / 2 + 1, -h / 2 - 3}}}) {
            auto* edge = CCLayerColor::create({Red.r, Red.g, Red.b, 255}, size.width, size.height);
            edge->setPosition(pos); m_ring->addChild(edge);
        }
        m_ring->setVisible(false); m_reveal->addChild(m_ring, 3);
    }
    void apply(float t) {
        auto const exiting = t >= m_time.exit;
        auto const exitFrame = [&](int index, float& y, float& alpha) {
            auto const p = std::clamp((t - m_time.exit - index * kSoloIntroExitStagger) / kSoloIntroExitSeconds, 0.f, 1.f);
            y -= soloEaseInCubic(p) * 31.f; alpha *= 1 - p;
        };
        for (auto const& card : m_cards) {
            float x = card.index * kSoloSlot, y = 0, alpha = 1;
            int slot = card.index;
            if (m_shift && t >= m_time.shift) {
                auto const p = soloEaseOutCubic((t - m_time.shift) / kSoloIntroShiftSeconds);
                x = (card.index - p) * kSoloSlot; slot = card.index - 1;
                if (slot < 0) alpha = 1 - std::clamp((t - m_time.shift) / 0.52f, 0.f, 1.f);
            }
            if (exiting && slot >= 0) exitFrame(slot, y, alpha);
            card.node->setPosition({x - kSoloSlot, y});
            setModeLobbyOpacity(card.node, static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255));
        }
        float y = 0, alpha = 1, scale = 1, shakeX = 0;
        if (m_shift) {
            if (t < m_time.slam) alpha = 0;
            else {
                auto const p = std::clamp((t - m_time.slam) / kSoloIntroSlamSeconds, 0.f, 1.f);
                auto const e = soloEaseInCubic(p);
                y = (1 - e) * 67.f; scale = 1.25f - 0.25f * e; alpha = std::clamp((t - m_time.slam) / 0.12f, 0.f, 1.f);
                auto const landed = t - m_time.slam - kSoloIntroSlamSeconds;
                if (landed >= 0 && landed < 0.32f) shakeX = 3.1f * (1 - landed / 0.32f) * std::sin(landed / 0.32f * 3.14159265f * 5);
                m_ring->setVisible(landed >= 0 && landed < 0.52f);
                if (m_ring->isVisible()) {
                    auto const q = landed / 0.52f;
                    m_ring->setScale(1 + 0.18f * q);
                    setModeLobbyOpacity(m_ring, static_cast<GLubyte>((1 - q) * 240));
                }
            }
        }
        if (exiting) exitFrame(m_revealSlot, y, alpha);
        auto const frame = soloRevolveFrame(t - m_time.spin);
        m_reveal->setPosition({(m_revealSlot - 1) * kSoloSlot + shakeX, y});
        m_reveal->setScale(scale);
        m_flip->setScaleX(frame.scaleX * frame.lift); m_flip->setScaleY(frame.lift);
        m_front->setVisible(frame.front); m_back->setVisible(!frame.front);
        setModeLobbyOpacity(m_reveal, static_cast<GLubyte>(std::clamp(alpha, 0.f, 1.f) * 255));
        auto const phase = t >= m_time.revealed ? 3 : t >= m_time.spin ? 2 : (m_shift && t >= m_time.shift) ? 1 : 0;
        if (phase != m_lastPhase) { m_lastPhase = phase; if (m_phase) m_phase(phase); }
        if (!m_boardFired && t >= m_time.board) { m_boardFired = true; if (m_onBoard) m_onBoard(); }
        if (!m_doneFired && t >= m_time.done) { m_doneFired = true; unscheduleUpdate(); if (m_onDone) m_onDone(); }
    }
public:
    // cards: the overview cards left to right (2 normally; 3 when a death match
    // shifts them). revealSlot is the revolving card's slot (always 2 on shift).
    static SoloRoundIntro* create(std::vector<SoloIntroCard> const& cards, SoloIntroReveal const& reveal, bool shift,
        int revealSlot, float elapsed, std::function<void(int)> phase, std::function<void()> onBoard, std::function<void()> onDone) {
        auto* node = new SoloRoundIntro;
        if (!node->initWithColor({0, 0, 0, 0}, 0, 0)) { delete node; return nullptr; }
        node->autorelease();
        node->m_shift = shift; node->m_time = soloIntroTimeline(shift); node->m_elapsed = elapsed;
        node->m_revealSlot = shift ? 2 : std::clamp(revealSlot, 0, 2);
        node->m_phase = std::move(phase); node->m_onBoard = std::move(onBoard); node->m_onDone = std::move(onDone);
        node->setCascadeOpacityEnabled(true);
        // Slots are centred on the node: x -164.4, 0, 164.4.
        // Cards fill the slots left to right, skipping the reveal slot (a
        // death-match shift keeps all three and moves them one slot left).
        int slot = 0;
        for (auto const& card : cards) {
            if (!shift && slot == node->m_revealSlot) ++slot;
            auto* built = roundCard(card);
            node->addChild(built, 3);
            node->m_cards.push_back({built, slot++});
        }
        node->buildReveal(reveal);
        node->apply(elapsed);
        node->scheduleUpdate();
        return node;
    }
    float elapsed() const { return m_elapsed; }
    void update(float dt) override {
        m_elapsed += std::min(dt, 0.1f);
        apply(m_elapsed);
    }
};
}
