#pragma once

#include "RankedGuideCarousel.hpp"
#include "RankedPatchNotesCache.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "../domain/PatchNotes.hpp"
#include <Geode/Geode.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace corum::ranked::ui {

// Kept across screen rebuilds (the dock's progress rebuilds the page).
struct UpdateTabsState { std::string key; bool guide = false; std::size_t version = 0; int korean = -1; };
inline UpdateTabsState& updateTabsState() { static UpdateTabsState state; return state; }

// Update screen, design C: [PATCH NOTES n | HOW RANKED WORKS] over the middle
// panel. PATCH NOTES lists the releases being installed (and the installed
// one) on the left and the chosen release's full notes on the right, KO or
// EN. Up / Down step the version and Tab switches the tab on a keyboard.
class RankedUpdateTabs final : public cocos2d::CCNode {
    cocos2d::CCSize m_size;
    std::string m_from, m_to;
    cocos2d::CCNode* m_chrome = nullptr;
    cocos2d::CCNode* m_patch = nullptr;
    RankedGuideCarousel* m_guide = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    UpdatePatchVersions m_versions;
    bool m_loaded = false, m_failed = false;
    static constexpr float kBar = 22.0f, kList = 96.0f, kPad = 10.0f;

    static float css(float px) { return px / 64.0f; }
    static cocos2d::CCLayerColor* box(cocos2d::CCNode* parent, float x, float y, float w, float h, cocos2d::ccColor3B color, int z = 1) {
        auto* layer = cocos2d::CCLayerColor::create({color.r, color.g, color.b, 255}, w, h);
        layer->setPosition({x, y}); parent->addChild(layer, z); return layer;
    }
    static cocos2d::CCLabelBMFont* text(cocos2d::CCNode* parent, std::string const& value, float cssPx, cocos2d::CCPoint at,
        cocos2d::ccColor3B color, FontWeight weight, cocos2d::CCPoint anchor = {0.0f, 0.5f}, float width = 0.0f) {
        auto* label = menuLabel(value, css(cssPx), at, color, weight);
        label->setAnchorPoint(anchor);
        if (width > 0.0f) fitMenuLabelWidth(label, width, css(cssPx), css(cssPx) * 0.55f);
        parent->addChild(label, 5); return label;
    }
    CCMenuItemSpriteExtra* item(cocos2d::CCNode* face, cocos2d::CCPoint at, cocos2d::SEL_MenuHandler handler, int tag, std::string const& id = {}) {
        auto* button = CCMenuItemSpriteExtra::create(face, this, handler);
        button->setPosition(at); button->setTag(tag); if (!id.empty()) button->setID(id);
        m_menu->addChild(button); return button;
    }
    [[nodiscard]] bool korean() const { return updateTabsState().korean != 0; }

    // A tab: caption (and the new-release count) with the accent underline when active.
    cocos2d::CCNode* tabFace(std::string const& caption, std::size_t count, bool active) {
        auto* face = cocos2d::CCNode::create();
        auto* label = menuLabel(caption, css(14.0f), {0.0f, kBar / 2.0f}, active ? Text : TextMuted, active ? FontWeight::Black : FontWeight::ExtraBold);
        label->setAnchorPoint({0.0f, 0.5f}); fitMenuLabelWidth(label, 150.0f, css(14.0f), css(14.0f) * 0.6f);
        auto width = label->getScaledContentSize().width + 20.0f;
        label->setPositionX(10.0f); face->addChild(label, 5);
        if (count > 0) {
            auto const chipX = width - 4.0f;
            auto* chip = surface({16.0f, 11.0f}, {chipX + 8.0f, kBar / 2.0f}, Accent, Accent, 255, false); face->addChild(chip, 4);
            auto* number = menuLabel(std::to_string(count), css(11.0f), {8.0f, 5.5f}, AccentInk, FontWeight::Black); chip->addChild(number, 5);
            width += 18.0f;
        }
        face->setContentSize({width, kBar});
        if (active) box(face, 0.0f, 0.0f, width, 2.0f, Accent, 6);
        return face;
    }
    static cocos2d::CCNode* segmentFace(std::string const& value, bool on) {
        auto* face = surface({28.0f, 14.0f}, {}, on ? Accent : Steel, on ? Accent : SurfaceRaised, 255, true);
        auto* label = menuLabel(value, css(11.0f), {14.0f, 7.0f}, on ? AccentInk : TextMuted, FontWeight::ExtraBold);
        face->addChild(label, 5); return face;
    }

    void rebuild() {
        auto& state = updateTabsState();
        m_menu->removeAllChildrenWithCleanup(true);
        if (m_chrome) m_chrome->removeAllChildrenWithCleanup(true);
        // Tab bar: PATCH NOTES (n new) | HOW RANKED WORKS, KO / EN on the right.
        auto* patchTab = tabFace(i18n::text("patch-notes", "PATCH NOTES"), m_versions.fresh, !state.guide);
        auto const patchW = patchTab->getContentSize().width;
        item(patchTab, {patchW / 2.0f, m_size.height - kBar / 2.0f}, menu_selector(RankedUpdateTabs::onTab), 0, "ranked-update-tab-patch"_spr);
        auto* guideTab = tabFace(i18n::text("guide-title", "HOW RANKED WORKS"), 0, state.guide);
        item(guideTab, {patchW + guideTab->getContentSize().width / 2.0f, m_size.height - kBar / 2.0f}, menu_selector(RankedUpdateTabs::onTab), 1, "ranked-update-tab-guide"_spr);
        if (!state.guide) {
            item(segmentFace("KO", korean()), {m_size.width - kPad - 44.0f, m_size.height - kBar / 2.0f}, menu_selector(RankedUpdateTabs::onLanguage), 1, "ranked-update-notes-ko"_spr);
            item(segmentFace("EN", !korean()), {m_size.width - kPad - 14.0f, m_size.height - kBar / 2.0f}, menu_selector(RankedUpdateTabs::onLanguage), 0, "ranked-update-notes-en"_spr);
        }
        if (m_guide) m_guide->setVisible(state.guide);
        m_patch->setVisible(!state.guide);
        if (!state.guide) renderPatch();
    }

    void renderPatch() {
        auto& state = updateTabsState();
        m_patch->removeAllChildrenWithCleanup(true);
        auto const top = m_size.height - kBar - 1.0f;
        box(m_patch, kList, 0.0f, 1.0f, top, SurfaceRaised, 2);
        if (!m_loaded || m_versions.list.empty()) {
            auto const message = !m_loaded ? (m_failed ? i18n::text("patch-notes-unavailable", "PATCH NOTES UNAVAILABLE") : i18n::text("patch-notes-loading", "LOADING PATCH NOTES..."))
                : i18n::text("patch-notes-none", "NO NOTES FOR THIS UPDATE");
            text(m_patch, message, 14.0f, {kList + kPad + 4.0f, top - 18.0f}, m_failed ? Red : TextMuted, FontWeight::SemiBold, {0.0f, 0.5f}, m_size.width - kList - 2.0f * kPad);
            return;
        }
        state.version = std::min(state.version, m_versions.list.size() - 1);
        // Version column: the new releases, then the installed one (dashed look: no fill).
        text(m_patch, i18n::text("update-versions", "VERSIONS"), 11.0f, {kPad, top - 9.0f}, Steel, FontWeight::ExtraBold);
        auto const rowH = 19.0f, rowW = kList - 2.0f * kPad;
        auto const rows = static_cast<std::size_t>(std::max(1.0f, std::floor((top - 34.0f) / (rowH + 3.0f))));
        auto const first = state.version >= rows ? state.version - rows + 1 : std::size_t{0};
        for (std::size_t i = first; i < m_versions.list.size() && i < first + rows; ++i) {
            auto const current = i >= m_versions.fresh, selected = i == state.version;
            auto const fill = selected ? cocos2d::ccColor3B{42, 48, 22} : current ? Surface : SurfaceRaised;
            auto* face = surface({rowW, rowH}, {}, selected ? Accent : current ? GhostEdge : SurfaceRaised, fill, 255, false);
            auto* version = menuLabel(updatePatchShortVersion(m_versions.list[i].version), css(13.0f), {6.0f, rowH / 2.0f}, current && !selected ? TextMuted : Text, FontWeight::ExtraBold);
            version->setAnchorPoint({0.0f, 0.5f}); fitMenuLabelWidth(version, rowW * 0.55f, css(13.0f), css(13.0f) * 0.6f); face->addChild(version, 5);
            if (i == 0 || current) {
                auto* tag = menuLabel(current ? i18n::text("update-version-current", "CURRENT") : i18n::text("update-version-new", "NEW"), css(10.0f),
                    {rowW - 5.0f, rowH / 2.0f}, current ? TextMuted : Accent, FontWeight::Black);
                tag->setAnchorPoint({1.0f, 0.5f}); fitMenuLabelWidth(tag, rowW * 0.4f, css(10.0f), css(10.0f) * 0.6f); face->addChild(tag, 5);
            }
            item(face, {kPad + rowW / 2.0f, top - 26.0f - static_cast<float>(i - first) * (rowH + 3.0f)}, menu_selector(RankedUpdateTabs::onVersion), static_cast<int>(i));
        }
        // The chosen release: version, date, then every line (scrolls when long).
        auto const& note = m_versions.list[state.version];
        auto const bodyX = kList + kPad + 4.0f, bodyW = m_size.width - bodyX - kPad;
        auto* title = text(m_patch, "v" + note.version, 20.0f, {bodyX, top - 14.0f}, Text, FontWeight::Black, {0.0f, 0.5f}, bodyW * 0.7f);
        leanLabel(title);
        text(m_patch, note.date, 11.0f, {bodyX + title->getScaledContentSize().width + 8.0f, top - 15.0f}, TextMuted, FontWeight::SemiBold);
        auto const viewH = top - 30.0f - 16.0f;
        std::vector<cocos2d::CCLabelTTF*> lines;
        float contentH = 0.0f;
        for (auto const& line : note.lines(korean())) {
            auto* body = cocos2d::CCLabelTTF::create(("* " + line).c_str(), patchNotesFont(), 9.0f, {bodyW - 6.0f, 0.0f}, cocos2d::kCCTextAlignmentLeft);
            if (!body) continue;
            body->setAnchorPoint({0.0f, 1.0f}); body->setColor({214, 218, 228});
            lines.push_back(body); contentH += body->getContentSize().height + 4.0f;
        }
        auto* scroll = geode::ScrollLayer::create({bodyW, viewH});
        scroll->setPosition({bodyX, 16.0f}); m_patch->addChild(scroll, 4);
        auto const height = std::max(viewH, contentH);
        scroll->m_contentLayer->setContentSize({bodyW, height});
        float y = height;
        for (auto* body : lines) { body->setPosition({0.0f, y}); scroll->m_contentLayer->addChild(body); y -= body->getContentSize().height + 4.0f; }
        scroll->scrollToTop();
#if defined(GEODE_IS_DESKTOP)
        text(m_patch, fmt::format("UP / DOWN  {}    TAB  {}", i18n::text("update-key-versions", "VERSION"), i18n::text("guide-title", "HOW RANKED WORKS")),
            10.0f, {bodyX, 8.0f}, TextMuted, FontWeight::SemiBold, {0.0f, 0.5f}, bodyW);
#endif
    }

    void update(float) override {
        auto& cache = RankedPatchNotesCache::get();
        if (m_loaded || (!cache.loaded() && cache.failed() == m_failed)) return;
        m_failed = cache.failed();
        if (cache.loaded()) {
            m_loaded = true; m_versions = updatePatchVersions(cache.notes(), m_from, m_to);
            // No notes for this update: open on the guide unless already chosen.
            if (m_versions.list.empty() && updateTabsState().version == 0) updateTabsState().guide = true;
        }
        rebuild();
    }

    bool init(cocos2d::CCSize size, std::string from, std::string to) {
        if (!CCNode::init()) return false;
        m_size = size; m_from = std::move(from); m_to = std::move(to);
        setContentSize(size); setID("ranked-update-tabs"_spr);
        auto& state = updateTabsState();
        if (state.key != m_from + ">" + m_to) state = {m_from + ">" + m_to, false, 0, i18n::isKorean() ? 1 : 0};
        box(this, 0.0f, 0.0f, size.width, size.height, Surface, 0);
        box(this, 0.0f, size.height - kBar - 1.0f, size.width, 1.0f, SurfaceRaised, 1);
        m_chrome = cocos2d::CCNode::create(); addChild(m_chrome, 2);
        m_patch = cocos2d::CCNode::create(); m_patch->setContentSize(size); addChild(m_patch, 3);
        if ((m_guide = RankedGuideCarousel::create({size.width, size.height - kBar - 1.0f}))) addChild(m_guide, 3);
        m_menu = cocos2d::CCMenu::create(); m_menu->setPosition(cocos2d::CCPointZero); addChild(m_menu, 10);
        auto& cache = RankedPatchNotesCache::get();
        cache.fetch();
        m_failed = cache.failed();
        if (cache.loaded()) { m_loaded = true; m_versions = updatePatchVersions(cache.notes(), m_from, m_to); }
        rebuild();
        scheduleUpdate();
        return true;
    }

public:
    static RankedUpdateTabs* create(cocos2d::CCSize size, std::string from, std::string to) {
        auto* node = new RankedUpdateTabs;
        if (node->init(size, std::move(from), std::move(to))) { node->autorelease(); return node; }
        delete node; return nullptr;
    }
    void onTab(cocos2d::CCObject* sender) {
        if (auto* node = typeinfo_cast<cocos2d::CCNode*>(sender)) { updateTabsState().guide = node->getTag() == 1; rebuild(); }
    }
    void onLanguage(cocos2d::CCObject* sender) {
        if (auto* node = typeinfo_cast<cocos2d::CCNode*>(sender)) { updateTabsState().korean = node->getTag(); rebuild(); }
    }
    void onVersion(cocos2d::CCObject* sender) {
        if (auto* node = typeinfo_cast<cocos2d::CCNode*>(sender)) { updateTabsState().version = static_cast<std::size_t>(std::max(0, node->getTag())); rebuild(); }
    }
    // Keyboard: Tab switches the tab, Up / Down step the version.
    void toggleTab() { updateTabsState().guide = !updateTabsState().guide; rebuild(); }
    bool stepVersion(int delta) {
        auto& state = updateTabsState();
        if (state.guide || m_versions.list.empty()) return false;
        auto const next = std::clamp<long>(static_cast<long>(state.version) + delta, 0, static_cast<long>(m_versions.list.size()) - 1);
        if (static_cast<std::size_t>(next) == state.version) return true;
        state.version = static_cast<std::size_t>(next); rebuild(); return true;
    }
};

} // namespace corum::ranked::ui
