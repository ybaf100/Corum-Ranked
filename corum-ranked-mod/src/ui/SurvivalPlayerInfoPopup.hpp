#pragma once

#include "../SurvivalRuntime.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankBadgeAssets.hpp"
#include <Geode/ui/Popup.hpp>
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;

// Survival owns its participant roster. Do not query Solo Ranked's current
// match/side when opening an icon from a Survival results or live-status board.
class SurvivalPlayerInfoPopup final : public Popup {
    std::string m_matchId;
    bool m_closing = false;

    void row(std::string const& key, std::string const& value, float y, ccColor3B color = Text) {
        auto* label = menuLabel(key, .30f, {16, y}, TextMuted, FontWeight::SemiBold);
        label->setAnchorPoint({0, .5f});
        fitMenuLabelWidth(label, 174, .30f, .20f);
        m_mainLayer->addChild(label, 11);
        auto* number = menuLabel(value, .34f, {282, y}, color, FontWeight::ExtraBold);
        number->setAnchorPoint({1, .5f});
        fitMenuLabelWidth(number, 105, .34f, .21f);
        m_mainLayer->addChild(number, 11);
    }

    bool init(SurvivalStanding const& player, std::string const& matchId) {
        if(!Popup::init(300, 238))return false;
        m_matchId = matchId;
        m_bgSprite->setVisible(false);
        m_closeBtn->setVisible(false);
        setID("survival-player-info-popup"_spr);
        auto* panel = surface({300, 238}, {150, 119}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(panel, 1);
        auto* title = menuLabel(i18n::text("player-info", "PLAYER INFO"), .49f, {142, 216}, Text, FontWeight::Black);
        fitMenuLabelWidth(title, 246, .49f, .32f);
        m_mainLayer->addChild(title, 11);
        auto* badge = createAnimatedRankBadge(player.tier, {24, 24}, RankBadgeDisplay::Compact);
        badge->setPosition({274, 215});
        m_mainLayer->addChild(badge, 11);
        auto* name = menuLabel(player.name, .37f, {150, 192}, Gold, FontWeight::ExtraBold);
        fitMenuLabelWidth(name, 268, .37f, .25f);
        m_mainLayer->addChild(name, 11);
        auto const deathmatch = SurvivalRuntime::get().view().phase == "TIEBREAK";
        row(i18n::text("survival-rank", "Rank"), player.rank>0 ? fmt::format("#{}",player.rank) : "-", 167, Gold);
        row("BEST", fmt::format("{}%", rankedScoreText(player.score, deathmatch)), 146);
        row("NOW", fmt::format("{}%", rankedScoreText(player.attemptActive ? player.currentProgress : 0, deathmatch)), 125);
        row(i18n::text("survival-attempts", "Attempts"), fmt::format("{}", player.attemptsUsed), 104);
        row(i18n::text("survival-tier", "Tier"), player.tier, 83, Cyan);
        row("GD Account", player.accountId>0 ? fmt::format("#{}", player.accountId) : (player.bot ? "BOT" : "-"), 62);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_mainLayer->addChild(menu, 20);
        auto* face = surface({124,26},{},Blue,SurfaceRaised,255,true);
        auto* text = menuLabel(i18n::text("close","CLOSE"),.32f,{62,13},Text,FontWeight::ExtraBold);
        face->addChild(text,5);
        auto* close = CCMenuItemSpriteExtra::create(face,this,menu_selector(SurvivalPlayerInfoPopup::onClose));
        close->setPosition({150,19});menu->addChild(close);
        scheduleUpdate();
        return true;
    }
public:
    static SurvivalPlayerInfoPopup* create(SurvivalStanding const& player, std::string const& matchId) {
        auto* popup = new SurvivalPlayerInfoPopup;
        if(popup->init(player,matchId)){popup->autorelease();return popup;}
        delete popup;return nullptr;
    }
    void update(float) override {
        if(!m_closing && (!SurvivalRuntime::get().active() || SurvivalRuntime::get().view().id!=m_matchId))
            onClose(nullptr);
    }
    void onClose(CCObject*) override {
        if(m_closing)return;
        m_closing=true;
        unscheduleUpdate();
        Popup::onClose(nullptr);
    }
};
} // namespace corum::ranked::ui
