#include "RankedRuntime.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"
#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <cmath>
using namespace geode::prelude;

class $modify(CorumRankedPauseLayer, PauseLayer) {
    struct Fields {
        std::string leaseId, matchId;
        int round = 0, deathmatch = 0;
        CCLabelBMFont* budget = nullptr;
        ~Fields() { corum::ranked::RankedRuntime::get().endMatchGameplayPause(leaseId); }
    };
    void customSetup() {
        PauseLayer::customSetup();
        auto* layer = PlayLayer::get();
        auto& ranked = corum::ranked::RankedRuntime::get();
        if (!layer || !layer->m_level || ranked.isPlacement() ||
            ranked.view().stage != corum::ranked::RuntimeStage::Matched ||
            !ranked.isGameplayLevel(static_cast<int>(layer->m_level->m_levelID))) return;
        m_fields->leaseId = ranked.beginMatchGameplayPause(static_cast<int>(layer->m_level->m_levelID));
        if (m_fields->leaseId.empty()) return;
        auto const& match = ranked.view().match;
        m_fields->matchId = match.matchId; m_fields->round = match.roundNumber; m_fields->deathmatch = match.deathmatchSequence;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); menu->setID("ranked-surrender-menu"_spr);
        addChild(menu, 100);
        auto* button = ButtonSprite::create("SURRENDER", 100, true,
            "bigFont.fnt", "GJ_button_06.png", 32, 0.55f);
        auto* item = CCMenuItemSpriteExtra::create(button, this, menu_selector(CorumRankedPauseLayer::onSurrender));
        item->setPosition({size.width / 2, 48}); menu->addChild(item);
        m_fields->budget = corum::ranked::ui::label("", 0.32f, {}, corum::ranked::ui::Text); m_fields->budget->setScale(0.32f);
        m_fields->budget->setPosition({size.width / 2, 77}); addChild(m_fields->budget, 100);
        schedule(schedule_selector(CorumRankedPauseLayer::watchBudget), 0.05f);
        watchBudget(0);
    }
    void watchBudget(float) {
        auto& ranked = corum::ranked::RankedRuntime::get(); ranked.tick();
        if (m_fields->leaseId.empty() || !getParent()) return;
        auto const remaining = ranked.matchPauseRemainingMs();
        auto const text = corum::ranked::i18n::isKorean()
            ? fmt::format("경기 일시정지: {:.1f} / 30초", std::ceil(remaining / 100) / 10)
            : fmt::format("MATCH PAUSE: {:.1f}s / 30s", std::ceil(remaining / 100) / 10);
        m_fields->budget->setString(text.c_str());
        if (remaining <= 0 || ranked.view().match.matchId != m_fields->matchId ||
            ranked.gameplayConceded(m_fields->round, m_fields->deathmatch)) resumeRankedPause(nullptr);
    }
    // Own callback, not another global onResume detour. User Resume is observed
    // at the existing gameplay frame boundary (and RAII scene cleanup).
    void resumeRankedPause(CCObject* sender) {
        corum::ranked::RankedRuntime::get().endMatchGameplayPause(m_fields->leaseId);
        unschedule(schedule_selector(CorumRankedPauseLayer::watchBudget));
        PauseLayer::onResume(sender);
    }
    void onSurrender(CCObject*) {
        auto const matchId = m_fields->matchId;
        auto const round = m_fields->round, deathmatch = m_fields->deathmatch;
        // Vanilla quick-popups use GD fonts. Keep these labels readable without
        // relying on glyphs from our separate Korean UI atlas.
        geode::createQuickPopup("Surrender",
            "Concede this round or the entire match?\nAlready earned scores are kept.",
            "ROUND", "MATCH",
            [matchId, round, deathmatch](auto*, bool wholeMatch) {
                geode::createQuickPopup("Confirm surrender",
                    wholeMatch || deathmatch > 0
                        ? "This ends the match as a defeat. Surrender?"
                        : "This concedes the current round. Surrender?",
                    "CANCEL", "SURRENDER",
                    [matchId, round, deathmatch, wholeMatch](auto*, bool confirmed) {
                        auto& ranked = corum::ranked::RankedRuntime::get(); auto const& match = ranked.view().match;
                        if (confirmed && match.matchId == matchId && match.roundNumber == round &&
                            match.deathmatchSequence == deathmatch) ranked.surrenderCurrentMatch(wholeMatch);
                    });
            });
    }
};
