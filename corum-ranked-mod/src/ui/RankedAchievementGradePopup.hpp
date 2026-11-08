#pragma once
#include "../domain/SurvivalEliminationEffects.hpp"
#include "RankedPopupLayout.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/ui/Popup.hpp>
#include <string>

namespace corum::ranked::ui {
using namespace geode::prelude;

// Diamond emblem in a tier colour (colour layers, so lobby fades reach it).
inline CCNode* achievementGradeEmblem(float size, ccColor3B tone, ccColor3B panel) {
    auto* root = CCNode::create();
    root->setContentSize({size, size});
    root->setAnchorPoint({0.5f, 0.5f});
    auto diamond = [&](float side, ccColor3B color, int z) {
        auto* layer = CCLayerColor::create({color.r, color.g, color.b, 255}, side, side);
        layer->ignoreAnchorPointForPosition(false);
        layer->setAnchorPoint({0.5f, 0.5f});
        layer->setPosition({size / 2, size / 2});
        layer->setRotation(45);
        root->addChild(layer, z);
    };
    diamond(size * 0.66f, tone, 1);
    diamond(size * 0.33f, panel, 2);
    diamond(size * 0.16f, tone, 3);
    return root;
}

inline ccColor3B achievementGradePanel(std::string_view tier) {
    if (tier == "RED") return {37, 12, 20};
    if (tier == "AQUA") return {10, 32, 38};
    if (tier == "BRONZE") return {36, 22, 10};
    if (tier == "SILVER") return {24, 28, 38};
    if (tier == "GOLD") return {35, 26, 5};
    return {18, 14, 32};
}

inline ccColor3B achievementGradeTone(std::string_view tier) {
    return tier.empty() ? ccColor3B{142, 134, 168} : tierColor(std::string(tier));
}

// The private achievement grade ladder: five grades, their claimed-AP
// thresholds and elimination effect unlocks, the current one marked.
class RankedAchievementGradePopup final : public Popup {
    bool m_closed = false, m_exited = false;
    bool init(int ap) {
        auto const grade = achievementApGrade(ap);
        auto const height = 120.f + kAchievementApGrades.size() * 30.f;
        auto const layout = rankedPopupLayout({330, height}, CCDirector::sharedDirector()->getWinSize());
        if (!Popup::init(layout.size.width, layout.size.height)) return false;
        m_noElasticity = true;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false); setID("ranked-achievement-grade-popup"_spr);
        auto* content = rankedPopupContent(m_mainLayer, m_buttonMenu, layout);
        auto const tone = achievementGradeTone(grade.tier);
        auto* panel = surface({330, height}, {165, height / 2}, tone, Surface, 255, true);
        content->addChild(panel);
        auto* title = menuLabel(i18n::text("achievement-grade-title", "ACHIEVEMENT GRADE"), .40f, {14, height - 20}, ccc3(255, 231, 166), FontWeight::Black);
        title->setAnchorPoint({0, 0.5f}); fitSingleLineMenuLabel(title, 200, 22, .40f); panel->addChild(title, 5);
        auto* only = menuLabel(i18n::text("achievement-grade-private", "Only you can see this"), .18f, {316, height - 20}, TextMuted, FontWeight::SemiBold);
        only->setAnchorPoint({1, 0.5f}); fitSingleLineMenuLabel(only, 100, 14, .18f); panel->addChild(only, 5);
        auto* basis = menuLabel(i18n::text("achievement-grade-basis", "Set by the AP you claimed (4,965 AP in all)"), .20f, {14, height - 40}, ccc3(200, 188, 230), FontWeight::SemiBold);
        basis->setAnchorPoint({0, 0.5f}); fitSingleLineMenuLabel(basis, 302, 14, .20f); panel->addChild(basis, 5);
        for (std::size_t i = 0; i < kAchievementApGrades.size(); ++i) {
            auto const [name, minAp] = kAchievementApGrades[i];
            auto const reached = ap >= minAp, current = grade.tier == name;
            auto const rowTone = tierColor(std::string(name));
            auto const y = height - 66 - 30.f * static_cast<float>(i);
            auto* row = surface({302, 26}, {165, y}, current ? rowTone : ccColor3B{42, 36, 64}, current ? achievementGradePanel(name) : ccColor3B{18, 12, 36}, reached ? 255 : 170, true);
            panel->addChild(row, 3);
            auto* emblem = achievementGradeEmblem(18, reached ? rowTone : ccColor3B{90, 84, 112}, current ? achievementGradePanel(name) : ccColor3B{18, 12, 36});
            emblem->setPosition({16, 13}); row->addChild(emblem, 5);
            auto* label = menuLabel(std::string(name), .26f, {32, 13}, reached ? rowTone : TextMuted, FontWeight::Black);
            label->setAnchorPoint({0, 0.5f}); fitSingleLineMenuLabel(label, 70, 16, .26f); row->addChild(label, 5);
            auto* threshold = menuLabel(fmt::format("{} AP", minAp), .22f, {108, 13}, reached ? Text : TextMuted, FontWeight::ExtraBold);
            threshold->setAnchorPoint({0, 0.5f}); fitSingleLineMenuLabel(threshold, 64, 14, .22f); row->addChild(threshold, 5);
            auto const unlock = name == "BRONZE" ? EliminationEffect::Shatter : name == "GOLD" ? EliminationEffect::Vortex : EliminationEffect::Default;
            if (unlock != EliminationEffect::Default) {
                auto const code = std::string(eliminationEffectCode(unlock));
                auto* reward = menuLabel(fmt::format(fmt::runtime(i18n::text("achievement-grade-unlock", "Unlocks {}")), i18n::text("elim-fx-" + code, code)),
                    .19f, {180, 13}, ccc3(200, 188, 230), FontWeight::SemiBold);
                reward->setAnchorPoint({0, 0.5f}); fitSingleLineMenuLabel(reward, 80, 14, .19f); row->addChild(reward, 5);
            }
            if (reached) {
                auto* mark = menuLabel(current ? i18n::text("achievement-grade-current", "CURRENT") : i18n::text("achievement-grade-reached", "REACHED"),
                    .18f, {294, 13}, rowTone, FontWeight::Black);
                mark->setAnchorPoint({1, 0.5f}); fitSingleLineMenuLabel(mark, 44, 14, .18f); row->addChild(mark, 5);
            }
        }
        auto const nextLine = grade.nextAp > 0
            ? fmt::format(fmt::runtime(i18n::text("achievement-grade-next", "{1} AP TO {0}")), grade.nextTier, grade.nextAp - ap)
            : i18n::text("achievement-grade-top", "Highest grade reached");
        auto* next = menuLabel(nextLine, .24f, {165, 46}, grade.nextAp > 0 ? achievementGradeTone(grade.nextTier) : Gold, FontWeight::Black);
        fitSingleLineMenuLabel(next, 302, 16, .24f); panel->addChild(next, 5);
        auto* face = surface({110, 26}, {}, Blue, SurfaceRaised, 255, true);
        auto* closeLabel = menuLabel(i18n::text("close", "CLOSE"), .28f, {55, 13}, Text, FontWeight::ExtraBold);
        fitSingleLineMenuLabel(closeLabel, 96, 18, .28f); face->addChild(closeLabel, 5);
        auto* close = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedAchievementGradePopup::onClose));
        close->setPosition({165, 20}); m_buttonMenu->setZOrder(20); m_buttonMenu->addChild(close);
        return true;
    }
public:
    static RankedAchievementGradePopup* create(int ap) {
        auto* p = new RankedAchievementGradePopup;
        if (p->init(ap)) { p->autorelease(); return p; }
        delete p; return nullptr;
    }
    void dismiss() {
        if (m_closed) return;
        [[maybe_unused]] Ref<RankedAchievementGradePopup> keepAlive(this);
        m_closed = true; Popup::onClose(nullptr);
    }
    void onClose(CCObject*) override { dismiss(); }
    void onExit() override {
        m_closed = true;
        if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};

} // namespace corum::ranked::ui
