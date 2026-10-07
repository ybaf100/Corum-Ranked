#pragma once
#include "../RankedRuntime.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/PauseLayer.hpp>
#include <Geode/ui/Popup.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;

class RankedSurrenderPopup final : public Popup {
    WeakRef<PauseLayer> m_owner;
    std::string m_matchId;
    int m_round = 0, m_deathmatch = 0;
    bool m_wholeMatch = false, m_confirmation = false, m_submitted = false, m_closed = false;
    bool m_showSubmissionStatus = false;
    CCNode* m_contents = nullptr;
    CCLabelBMFont* m_status = nullptr;

    void action(CCMenu* menu, std::string const& caption, CCPoint position,
        ccColor3B accent, SEL_MenuHandler callback, float width = 130) {
        auto* face = surface({width, 35}, {}, accent, SurfaceRaised, 250, true);
        auto* text = label(caption, 0.31f, {width / 2, 17.5f}, Text, FontWeight::ExtraBold);
        text->limitLabelWidth(width - 14, 0.31f, 0.18f); face->addChild(text, 5);
        auto* item = CCMenuItemSpriteExtra::create(face, this, callback);
        item->setPosition(position); menu->addChild(item);
    }
    bool init(PauseLayer* owner, std::string matchId, int round, int deathmatch) {
        if (!Popup::init(370, 235)) return false;
        m_owner = owner; m_matchId = std::move(matchId); m_round = round; m_deathmatch = deathmatch;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-surrender-popup"_spr);
        render(); schedule(schedule_selector(RankedSurrenderPopup::watchContext), 0.05f);
        return true;
    }
    void render() {
        if (m_contents) m_contents->removeFromParentAndCleanup(true);
        m_contents = surface({370, 235}, {185, 117.5f}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(m_contents, 10);
        auto title = i18n::text(m_confirmation ? "surrender-confirm" : "surrender",
            m_confirmation ? "CONFIRM SURRENDER" : "SURRENDER");
        auto* header = label(title, 0.55f, {185, 202}, Text, FontWeight::Black);
        header->limitLabelWidth(330, 0.55f, 0.32f); m_contents->addChild(header, 10);
        auto const warning = m_confirmation
            ? i18n::text(m_wholeMatch || m_deathmatch > 0 ? "surrender-match-warning" : "surrender-round-warning",
                m_wholeMatch || m_deathmatch > 0 ? "End this match as a defeat?" : "Concede this round?")
            : i18n::text("surrender-choice", "Concede this round or the entire match.");
        auto* detail = label(warning, 0.27f, {185, 157}, Text);
        detail->limitLabelWidth(332, 0.27f, 0.16f); m_contents->addChild(detail, 10);
        auto* kept = label(i18n::text("surrender-score-kept", "Already earned scores are kept."),
            0.23f, {185, 133}, TextMuted); kept->limitLabelWidth(332, 0.23f, 0.16f);
        m_contents->addChild(kept, 10);
        m_status = label("", 0.25f, {185, 108}, Gold); m_contents->addChild(m_status, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_contents->addChild(menu, 20);
        if (m_confirmation) {
            action(menu, i18n::text("surrender", "SURRENDER"), {185, 72}, Red,
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
        return ranked.view().stage == RuntimeStage::Matched && !ranked.isSpectating() &&
            match.matchId == m_matchId && match.roundNumber == m_round && match.deathmatchSequence == m_deathmatch;
    }
    void onRound(CCObject*) { if (!m_submitted) { m_wholeMatch = false; m_confirmation = true; render(); } }
    void onMatch(CCObject*) { if (!m_submitted) { m_wholeMatch = true; m_confirmation = true; render(); } }
    void onConfirm(CCObject*) {
        if (m_submitted || !sameRound()) return;
        m_submitted = true; m_showSubmissionStatus = true;
        RankedRuntime::get().surrenderCurrentMatch(m_wholeMatch); watchContext(0);
    }
    void onCancel(CCObject*) { dismiss(); }
    void watchContext(float) {
        auto owner = m_owner.lock(); auto& ranked = RankedRuntime::get();
        // Remove the modal before a timeout resumes gameplay underneath it.
        if (!owner || !owner->getParent() || !sameRound() || ranked.matchPauseRemainingMs() <= 0 ||
            ranked.gameplayConceded(m_round, m_deathmatch)) { dismiss(); return; }
        using Status = RankedRuntime::SurrenderStatus;
        auto const status = ranked.surrenderStatus(); std::string text;
        if (m_showSubmissionStatus) switch (status) {
            case Status::WaitingAttempt: text = i18n::text("surrender-waiting-attempt", "Syncing attempt before surrender..."); break;
            case Status::Sending: text = i18n::text("surrender-sending", "Sending surrender..."); break;
            case Status::Retrying: text = i18n::text("surrender-retrying", "Connection issue - retrying surrender..."); break;
            case Status::Accepted: text = i18n::text("surrender-accepted", "Surrender accepted"); break;
            case Status::Rejected:
                text = i18n::text("surrender-rejected", "Surrender rejected. Check match state and retry.");
                m_submitted = false; break;
            case Status::Idle: break;
        }
        m_status->setString(text.c_str()); m_status->limitLabelWidth(336, 0.25f, 0.15f);
    }
public:
    static RankedSurrenderPopup* create(PauseLayer* owner, std::string matchId, int round, int deathmatch) {
        auto* popup = new RankedSurrenderPopup;
        if (popup->init(owner, std::move(matchId), round, deathmatch)) { popup->autorelease(); return popup; }
        delete popup; return nullptr;
    }
    void dismiss() {
        if (m_closed) return;
        m_closed = true;
        unschedule(schedule_selector(RankedSurrenderPopup::watchContext)); Popup::onClose(nullptr);
    }
};
}
