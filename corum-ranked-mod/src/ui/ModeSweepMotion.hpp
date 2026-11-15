#pragma once

#include "../domain/ModeSweepTimeline.hpp"
#include "RankedVisuals.hpp"

#include <Geode/Geode.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_set>
#include <utility>
#include <vector>

namespace corum::ranked::ui {

// One moving, fading piece of the sweep. GD paints a CCLayerColor from its
// own opacity, so surfaces are faded from the opacity each had when the
// motion started; everything else takes a displayed opacity. A trophy that
// flies on its own is skipped.
class SweepPiece {
public:
    SweepPiece() = default;
    explicit SweepPiece(cocos2d::CCNode* node, std::vector<cocos2d::CCNode*> skip = {}) : m_node(node) {
        if (!node) return;
        for (auto* s : skip) if (s) m_skip.insert(s);
        m_home = node->getPosition();
        collect(node);
    }
    [[nodiscard]] cocos2d::CCNode* node() const { return m_node.data(); }
    [[nodiscard]] cocos2d::CCPoint home() const { return m_home; }
    void place(float dx, float dy) const {
        if (m_node) m_node->setPosition({m_home.x + dx, m_home.y + dy});
    }
    void fade(float alpha) const {
        if (!m_node) return;
        alpha = std::clamp(alpha, 0.0f, 1.0f);
        for (auto const& [layer, opacity] : m_layers) layer->setOpacity(static_cast<GLubyte>(std::lround(opacity * alpha)));
        propagate(m_node.data(), static_cast<GLubyte>(std::lround(255.0f * alpha)));
    }

private:
    geode::Ref<cocos2d::CCNode> m_node;
    cocos2d::CCPoint m_home{};
    std::vector<std::pair<geode::Ref<cocos2d::CCLayerColor>, GLubyte>> m_layers;
    std::unordered_set<cocos2d::CCNode*> m_skip, m_surfaces;

    void collect(cocos2d::CCNode* node) {
        if (m_skip.contains(node)) return;
        if (auto* layer = geode::cast::typeinfo_cast<cocos2d::CCLayerColor*>(node)) {
            auto const size = layer->getContentSize();
            if (size.width > 0.0f && size.height > 0.0f) {
                m_layers.emplace_back(geode::Ref<cocos2d::CCLayerColor>(layer), layer->getOpacity());
                m_surfaces.insert(node);
            }
        }
        for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) collect(child);
    }
    // A faded surface already carries the fade in its own opacity: it passes
    // that (not the fade twice) to cascading children.
    void propagate(cocos2d::CCNode* node, GLubyte inherited) const {
        if (m_skip.contains(node)) return;
        if (auto* rgba = dynamic_cast<cocos2d::CCRGBAProtocol*>(node)) {
            rgba->updateDisplayedOpacity(m_surfaces.contains(node) ? 255 : inherited);
            if (rgba->isCascadeOpacityEnabled()) inherited = rgba->getDisplayedOpacity();
        }
        for (auto* child : geode::cocos::CCArrayExt<cocos2d::CCNode*>(node->getChildren())) propagate(child, inherited);
    }
};

// Main menu <-> mode lobby "seam sweep" (domain/ModeSweepTimeline.hpp). The
// outgoing screen's nodes are adopted by this node before the new screen is
// built, so both live together for the 0.78 s; when it ends this node removes
// itself with them. Leaving the lobby plays the same frames backwards.
class ModeSweepMotion final : public cocos2d::CCNode {
public:
    struct Setup {
        bool opening = true;
        float width = 0.0f;                         // screen width (points)
        float selectedShift = 0.0f;                 // menu half -> lobby card (x)
        float otherShift = 0.0f;                    // other half's slide off (signed x)
        cocos2d::CCNode* menuSelected = nullptr;    // main menu halves
        cocos2d::CCNode* menuOther = nullptr;
        cocos2d::CCNode* card = nullptr;            // mode lobby card (left column)
        cocos2d::CCNode* enter = nullptr;           // the tapped ENTER slab (opening)
        cocos2d::CCNode* menuTrophy = nullptr;      // child of menuSelected
        cocos2d::CCNode* lobbyTrophy = nullptr;     // child of card
        float trophyRatio = 1.0f;                   // menu trophy size / lobby trophy size
        std::vector<std::vector<cocos2d::CCNode*>> bands; // lobby panel rows, top to bottom
        cocos2d::CCNode* hero = nullptr;            // START button
        std::vector<cocos2d::CCNode*> menuTitles, lobbyTitles; // header titles (rise / settle)
        std::vector<cocos2d::CCNode*> menuHeader, lobbyHeader; // other header pieces (fade)
        std::function<void()> done;
    };

    static ModeSweepMotion* create() {
        auto* node = new ModeSweepMotion;
        if (!node->init()) { delete node; return nullptr; }
        node->setID("ranked-mode-sweep"_spr);
        node->autorelease();
        return node;
    }

    // Keeps an outgoing node alive under this one (same place: both parents
    // sit at the origin), out of the screen root that is about to be rebuilt.
    void adopt(cocos2d::CCNode* node) {
        if (!node) return;
        geode::Ref<cocos2d::CCNode> keep(node);
        auto const z = node->getZOrder();
        auto const at = node->getParent() ? node->getParent()->convertToWorldSpace(node->getPosition()) : node->getPosition();
        node->removeFromParentAndCleanup(false);
        node->setPosition(at);
        addChild(node, z);
    }

    void start(Setup setup) {
        m_setup = std::move(setup);
        auto const& s = m_setup;
        m_rowShift = s.width * 40.0f / 568.0f;
        m_heroShift = s.width * 70.0f / 568.0f;
        // The trophy flies alone, so neither half fades it.
        m_fly = s.menuTrophy && s.lobbyTrophy;
        std::vector<cocos2d::CCNode*> trophies;
        if (m_fly) trophies = {s.menuTrophy, s.lobbyTrophy};
        m_selected = SweepPiece(s.menuSelected, trophies);
        m_other = SweepPiece(s.menuOther);
        m_card = SweepPiece(s.card, trophies);
        m_hero = SweepPiece(s.hero);
        if (s.enter) m_enterScale = s.enter->getScale();
        if (m_fly) {
            m_menuTrophyHome = s.menuTrophy->getPosition(); m_menuTrophyScale = s.menuTrophy->getScale();
            m_lobbyTrophyHome = s.lobbyTrophy->getPosition(); m_lobbyTrophyScale = s.lobbyTrophy->getScale();
        }
        for (auto const& band : s.bands) {
            std::vector<SweepPiece> pieces;
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f;
            for (auto* item : band) {
                if (!item) continue;
                pieces.emplace_back(item);
                auto const box = item->boundingBox();
                auto const origin = item->getParent() ? item->getParent()->convertToWorldSpace(box.origin) : box.origin;
                minX = std::min(minX, origin.x); maxX = std::max(maxX, origin.x + box.size.width); minY = std::min(minY, origin.y);
            }
            if (pieces.empty()) continue;
            // The signal-colour line drawn under the row as it arrives.
            auto* line = cocos2d::CCLayerColor::create({Accent.r, Accent.g, Accent.b, 0}, 0.0f, 1.5f);
            line->setPosition({minX, minY - 3.0f});
            addChild(line, 40);
            m_bands.push_back({std::move(pieces), geode::Ref<cocos2d::CCLayerColor>(line), std::max(0.0f, maxX - minX)});
        }
        for (auto* n : s.menuTitles) m_menuTitles.emplace_back(n);
        for (auto* n : s.lobbyTitles) m_lobbyTitles.emplace_back(n);
        for (auto* n : s.menuHeader) m_menuHeader.emplace_back(n);
        for (auto* n : s.lobbyHeader) m_lobbyHeader.emplace_back(n);
        apply(modeSweepTime(0.0f, s.opening));
        scheduleUpdate();
    }

    void update(float dt) override {
        if (m_finished) return;
        m_elapsed += std::clamp(dt, 0.0f, 0.05f) * 1000.0f;
        apply(modeSweepTime(m_elapsed, m_setup.opening));
        if (m_elapsed >= ModeSweepMs) finish();
    }

    // Ends at once on the final frame (also when the screen is torn down).
    void finish() {
        if (m_finished) return;
        m_finished = true;
        geode::Ref<ModeSweepMotion> keep(this);
        apply(m_setup.opening ? ModeSweepMs : 0.0f);
        unscheduleUpdate();
        auto done = std::move(m_setup.done);
        removeFromParentAndCleanup(true);
        if (done) done();
    }

private:
    struct Band { std::vector<SweepPiece> pieces; geode::Ref<cocos2d::CCLayerColor> line; float width = 0.0f; };
    Setup m_setup;
    SweepPiece m_selected, m_other, m_card, m_hero;
    std::vector<Band> m_bands;
    std::vector<SweepPiece> m_menuTitles, m_lobbyTitles, m_menuHeader, m_lobbyHeader;
    float m_elapsed = 0.0f, m_rowShift = 0.0f, m_heroShift = 0.0f, m_enterScale = 1.0f;
    float m_menuTrophyScale = 1.0f, m_lobbyTrophyScale = 1.0f;
    cocos2d::CCPoint m_menuTrophyHome{}, m_lobbyTrophyHome{};
    bool m_fly = false, m_finished = false;

    void apply(float t) {
        auto const& s = m_setup;
        auto const f = modeSweepFrame(t);
        if (s.opening && s.enter) s.enter->setScale(m_enterScale * f.press);
        m_other.place(s.otherShift * f.otherOut, 0.0f); m_other.fade(f.otherAlpha);
        m_selected.place(s.selectedShift * f.travel, 0.0f); m_selected.fade(f.selAlpha);
        m_card.place(-s.selectedShift * (1.0f - f.travel), 0.0f); m_card.fade(f.cardAlpha);
        if (m_fly) placeTrophies(f);
        auto const count = static_cast<int>(m_bands.size());
        for (int i = 0; i < count; ++i) {
            auto const b = modeSweepBand(t, i, count);
            for (auto const& piece : m_bands[i].pieces) { piece.place(m_rowShift * b.offset, 0.0f); piece.fade(b.alpha); }
            m_bands[i].line->setContentSize({m_bands[i].width * b.wipe, 1.5f});
            m_bands[i].line->setOpacity(static_cast<GLubyte>(std::lround(255.0f * b.wipeAlpha)));
        }
        m_hero.place(m_heroShift * f.heroOffset, 0.0f); m_hero.fade(f.heroAlpha);
        constexpr float rise = 12.0f;
        for (auto const& p : m_menuTitles) { p.place(0.0f, rise * f.header); p.fade(1.0f - f.header); }
        for (auto const& p : m_lobbyTitles) { p.place(0.0f, -rise * (1.0f - f.header)); p.fade(f.header); }
        for (auto const& p : m_menuHeader) p.fade(1.0f - f.header);
        for (auto const& p : m_lobbyHeader) p.fade(f.header);
    }

    // The incoming screen's trophy flies between the two places; the
    // outgoing one shows only while the flight has not reached it.
    void placeTrophies(ModeSweepFrame const& f) {
        auto const& s = m_setup;
        auto* flyer = s.opening ? s.lobbyTrophy : s.menuTrophy;
        s.menuTrophy->setVisible(f.menuTrophyHome || (flyer == s.menuTrophy && f.flying));
        s.lobbyTrophy->setVisible(f.lobbyTrophyHome || (flyer == s.lobbyTrophy && f.flying));
        auto* menuGroup = s.menuSelected, *lobbyGroup = s.card;
        if (!f.flying) {
            s.menuTrophy->setPosition(m_menuTrophyHome); s.menuTrophy->setScale(m_menuTrophyScale);
            s.lobbyTrophy->setPosition(m_lobbyTrophyHome); s.lobbyTrophy->setScale(m_lobbyTrophyScale);
            return;
        }
        // Both places in screen points (each group's resting position is its home).
        auto const from = cocos2d::CCPoint{m_selected.home().x + m_menuTrophyHome.x, m_selected.home().y + m_menuTrophyHome.y};
        auto const to = cocos2d::CCPoint{m_card.home().x + m_lobbyTrophyHome.x, m_card.home().y + m_lobbyTrophyHome.y};
        auto const at = cocos2d::CCPoint{from.x + (to.x - from.x) * f.flight, from.y + (to.y - from.y) * f.flight + 14.0f * f.lift};
        auto* group = flyer == s.menuTrophy ? menuGroup : lobbyGroup;
        flyer->setPosition({at.x - group->getPositionX(), at.y - group->getPositionY()});
        // Its size goes from the menu trophy's to the lobby trophy's.
        auto const ratio = std::max(0.01f, s.trophyRatio);
        auto const base = flyer == s.menuTrophy ? m_menuTrophyScale : m_lobbyTrophyScale;
        auto const atMenu = flyer == s.menuTrophy ? 1.0f : ratio, atLobby = flyer == s.menuTrophy ? 1.0f / ratio : 1.0f;
        flyer->setScale(base * (atMenu + (atLobby - atMenu) * f.flight));
    }
};

} // namespace corum::ranked::ui
