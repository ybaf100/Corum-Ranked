#pragma once
#include "../RankedRuntime.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/ui/Popup.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;

class RankedSurrenderPopup final : public Popup {
    // reset() runs WeakRef's destructor (Geode 5.10.1's pointer assignment to
    // nullptr only drops its controller). Release the native owner on EXIT.
    std::optional<WeakRef<PauseLayer>> m_owner;
    std::string m_matchId;
    int m_round = 0, m_deathmatch = 0;
    int m_placementMapIndex = 0;
    bool m_placement = false;
    std::optional<WeakRef<PlayLayer>> m_playOwner;
    bool m_wholeMatch = false, m_confirmation = false, m_submitted = false, m_closed = false;
    bool m_showSubmissionStatus = false;
    CCNode* m_contents = nullptr;
    CCLabelBMFont* m_status = nullptr;

    void action(CCMenu* menu, std::string const& caption, CCPoint position,
        ccColor3B accent, SEL_MenuHandler callback, float width = 130) {
        auto* face = surface({width, 35}, {}, accent, SurfaceRaised, 250, true);
        auto* text = label(caption, 0.31f, {width / 2, 17.5f}, Text, FontWeight::ExtraBold);
        corum::ranked::ui::fitLabelWidth(text, width - 14, 0.31f, 0.18f); face->addChild(text, 5);
        auto* item = CCMenuItemSpriteExtra::create(face, this, callback);
        item->setPosition(position); menu->addChild(item);
    }
    bool init(PauseLayer* owner, std::string matchId, int round, int deathmatch, bool placement, int mapIndex) {
        if (!Popup::init(370, 235)) return false;
        m_owner.emplace(owner); m_matchId = std::move(matchId); m_round = round; m_deathmatch = deathmatch;
        m_placement = placement; m_placementMapIndex = mapIndex;
        if (placement) { m_confirmation = true; m_playOwner.emplace(PlayLayer::get()); }
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-surrender-popup"_spr);
        render(); schedule(schedule_selector(RankedSurrenderPopup::watchContext), 0.05f);
        return true;
    }
    void render() {
        if (m_contents) m_contents->removeFromParentAndCleanup(true);
        m_contents = surface({370, 235}, {185, 117.5f}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(m_contents, 10);
        auto title = m_placement ? i18n::text("placement-skip-map", "SKIP MAP") : i18n::text(m_confirmation ? "surrender-confirm" : "surrender",
            m_confirmation ? "CONFIRM SURRENDER" : "SURRENDER");
        auto* header = label(title, 0.55f, {185, 202}, Text, FontWeight::Black);
        corum::ranked::ui::fitLabelWidth(header, 330, 0.55f, 0.32f); m_contents->addChild(header, 10);
        auto const warning = m_placement ? i18n::text("placement-skip-map-warning", "Finish only this map with the score earned so far?") : m_confirmation
            ? i18n::text(m_wholeMatch || m_deathmatch > 0 ? "surrender-match-warning" : "surrender-round-warning",
                m_wholeMatch || m_deathmatch > 0 ? "End this match as a defeat?" : "Concede this round?")
            : i18n::text("surrender-choice", "Concede this round or the entire match.");
        auto* detail = label(warning, 0.27f, {185, 157}, Text);
        corum::ranked::ui::fitLabelWidth(detail, 332, 0.27f, 0.16f); m_contents->addChild(detail, 10);
        auto* kept = label(i18n::text("surrender-score-kept", "Already earned scores are kept."),
            0.23f, {185, 133}, TextMuted); corum::ranked::ui::fitLabelWidth(kept, 332, 0.23f, 0.16f);
        m_contents->addChild(kept, 10);
        m_status = label("", 0.25f, {185, 108}, Gold); m_contents->addChild(m_status, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_contents->addChild(menu, 20);
        if (m_confirmation) {
            action(menu, m_placement ? i18n::text("placement-skip-map", "SKIP MAP") : i18n::text("surrender", "SURRENDER"), {185, 72}, Red,
                menu_selector(RankedSurrenderPopup::onConfirm), 275);
        } else {
            action(menu, i18n::text("surrender-round", "ROUND"), {113, 72}, Orange,
                menu_selector(RankedSurrenderPopup::onRound));
            action(menu, i18n::text("surrender-match", "MATCH"), {257, 72}, Red,
                menu_selector(RankedSurrenderPopup::onMatch));
        }
        action(menu, i18n::text("cancel", "CANCEL"), {185, 29}, Blue,
            menu_selector(RankedSurrenderPopup::onCancel), 275);
    }
    bool sameRound() const {
        auto const& ranked = RankedRuntime::get(); auto const& match = ranked.view().match;
        if (m_placement) {
            auto owner = m_playOwner ? m_playOwner->lock() : Ref<PlayLayer>();
            auto const& placement = ranked.view().placement;
            return owner && owner.data() == PlayLayer::get() && ranked.isPlacement() &&
                !ranked.isSpectating() && placement.runId == m_matchId &&
                placement.currentStageIndex == m_round && placement.currentMapIndex == m_placementMapIndex;
        }
        return ranked.view().stage == RuntimeStage::Matched && !ranked.isSpectating() &&
            match.matchId == m_matchId && match.roundNumber == m_round && match.deathmatchSequence == m_deathmatch;
    }
    void onRound(CCObject*) { if (!m_submitted) { m_wholeMatch = false; m_confirmation = true; render(); } }
    void onMatch(CCObject*) { if (!m_submitted) { m_wholeMatch = true; m_confirmation = true; render(); } }
    void onConfirm(CCObject*) {
        if (m_submitted || !sameRound()) return;
        m_submitted = true; m_showSubmissionStatus = true;
        if (m_placement) {
            auto owner = m_playOwner ? m_playOwner->lock() : Ref<PlayLayer>();
            if (!owner || !owner->m_level || owner->m_isPracticeMode || owner->m_isTestMode) { m_submitted = false; return; }
            auto const progress = std::clamp(std::max(static_cast<double>(owner->getCurrentPercent()),
                static_cast<double>(owner->getCurrentPercentInt())), 0.0, 100.0);
            RankedRuntime::get().skipPlacementMap(static_cast<int>(owner->m_level->m_levelID), progress);
        } else RankedRuntime::get().surrenderCurrentMatch(m_wholeMatch);
        watchContext(0);
    }
    void onCancel(CCObject*) { dismiss(); }
    void watchContext(float) {
        if (m_closed) return;
        auto owner = m_owner ? m_owner->lock() : Ref<PauseLayer>(); auto& ranked = RankedRuntime::get();
        // Remove the modal before a timeout resumes gameplay underneath it.
        if (!owner || !owner->getParent() || !sameRound() || (m_placement
            ? ranked.placementMapSkipped(m_matchId, m_round, m_placementMapIndex)
            : ranked.matchPauseRemainingMs() <= 0 || ranked.gameplayConceded(m_round, m_deathmatch))) { dismiss(); return; }
        using Status = RankedRuntime::SurrenderStatus;
        auto const status = m_placement ? ranked.placementMapSkipStatus() : ranked.surrenderStatus(); std::string text;
        if (m_showSubmissionStatus) switch (status) {
            case Status::WaitingAttempt: text = m_placement ? i18n::text("placement-skip-syncing", "Saving this attempt...") : i18n::text("surrender-waiting-attempt", "Syncing attempt before surrender..."); break;
            case Status::Sending: text = m_placement ? i18n::text("placement-skip-sending", "Skipping this map...") : i18n::text("surrender-sending", "Sending surrender..."); break;
            case Status::Retrying: text = m_placement ? i18n::text("placement-skip-retrying", "Connection issue - retrying map skip...") : i18n::text("surrender-retrying", "Connection issue - retrying surrender..."); break;
            case Status::Accepted: text = m_placement ? i18n::text("placement-skip-accepted", "Map skipped") : i18n::text("surrender-accepted", "Surrender accepted"); break;
            case Status::Rejected:
                text = m_placement ? i18n::text("placement-skip-rejected", "Map skip rejected. Check the map and retry.") : i18n::text("surrender-rejected", "Surrender rejected. Check match state and retry.");
                m_submitted = false; break;
            case Status::Idle: break;
        }
        m_status->setString(text.c_str()); corum::ranked::ui::fitLabelWidth(m_status, 336, 0.25f, 0.15f);
    }
public:
    void onExit() override {
        m_closed = true;
        unschedule(schedule_selector(RankedSurrenderPopup::watchContext));
        // Geode 5.10.1 Popup releases this registration only in its destructor.
        // A closed Popup can survive there in WeakRefPool; release OUR modal
        // registration at scene exit rather than shifting the next round's UI.
        CCTouchDispatcher::get()->unregisterForcePrio(this);
        m_owner.reset();
        m_playOwner.reset();
        Popup::onExit();
    }
    void onClose(CCObject*) override { dismiss(); }
    static RankedSurrenderPopup* create(PauseLayer* owner, std::string matchId, int round, int deathmatch,
        bool placement = false, int mapIndex = 0) {
        auto* popup = new RankedSurrenderPopup;
        if (popup->init(owner, std::move(matchId), round, deathmatch, placement, mapIndex)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void dismiss() {
        if (m_closed) return;
        m_closed = true;
        m_owner.reset();
        m_playOwner.reset();
        unschedule(schedule_selector(RankedSurrenderPopup::watchContext)); Popup::onClose(nullptr);
    }
};
}
