#include "PublicPlayerProfile.hpp"
#include "RankedPopupLayout.hpp"
#pragma once
#include <tuple>

#include "../RankedRuntime.hpp"
#include "../domain/PlayerInfoPresentation.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include "RankBadgeAssets.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/ProfilePage.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>
#include <Geode/ui/Popup.hpp>

namespace corum::ranked::ui {
using namespace geode::prelude;

class RankedPlayerInfoPopup final : public Popup, public UserInfoDelegate {
    std::string m_matchId, m_side;
    int m_accountId = 0, m_requests = 0;
    bool m_closed = false, m_exited = false, m_inFlight = false, m_lookupFailed = false;
    std::uint64_t m_revision = 0;
    std::chrono::steady_clock::time_point m_requestedAt {}, m_nextRequest {};
    std::chrono::steady_clock::time_point m_nextForumRead {};
    std::chrono::steady_clock::time_point m_lookupDeadline {};
    int m_forumReads = 0;
    struct Statistics { int globalRank, stars, moons, demons; std::optional<int> extreme; };
    std::optional<Statistics> m_statistics;
    CCNode* m_contents = nullptr;
    CCNode* m_panel = nullptr;

    bool init(std::string side) {
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched || view.match.state == "ROUND_ENTERING" ||
            view.match.state == "DEATHMATCH_ENTERING" || view.match.state == "ROUND_PLAYING" ||
            view.match.state == "DEATHMATCH_PLAYING") return false;
        // Q2 board: a landscape .pop (800 x 540 board px = 400 x 270).
        auto const layout=rankedPopupLayout({400,270},CCDirector::sharedDirector()->getWinSize());
        if (!Popup::init(layout.size.width,layout.size.height)) return false;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-player-info-popup"_spr);
        m_side = std::move(side); m_matchId = RankedRuntime::get().view().match.matchId;
        RankedRuntime::get().fetchPlayerInfo(m_side);
        m_accountId = RankedRuntime::get().view().playerInfo.accountId;
        if (m_accountId <= 0) return false;
        m_lookupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        m_panel = rankedPopupContent(m_mainLayer,m_buttonMenu,layout);
        m_panel->addChild(surface({400,270},{200,135},Accent,Surface,255,true));
        auto* menu=m_buttonMenu;menu->setZOrder(30);
        // Q2 header .xbtn, and the ghost GD PROFILE button on the footer row.
        auto* close = CCMenuItemSpriteExtra::create(publicCloseFace(), this, menu_selector(RankedPlayerInfoPopup::onClose));
        close->setPosition({376, 252}); menu->addChild(close);
        auto* face = surface({96, 18}, {}, Steel, SurfaceRaised, 250, true);
        auto* caption = menuLabel(i18n::text("info-open-gd-profile", "OPEN GD PROFILE"), 14.f / 64.f, {48, 9}, Text, FontWeight::ExtraBold);
        fitMenuLabelWidth(caption, 86, 14.f / 64.f, 0.12f); face->addChild(caption, 5);
        auto* profile = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedPlayerInfoPopup::onGdProfile));
        profile->setPosition({338, 54}); menu->addChild(profile);
        schedule(schedule_selector(RankedPlayerInfoPopup::refresh), 0.1f); render(); refresh(0);
        return true;
    }
    void releaseDelegate() {
        auto* manager = GameLevelManager::sharedState();
        if (manager && manager->m_userInfoDelegate == this) manager->m_userInfoDelegate = nullptr;
        m_inFlight = false;
    }
    void resolveStatistics() {
        if (m_statistics || m_closed) return;
        auto* manager = GameLevelManager::sharedState();
        if (!manager) return;
        if (auto* score = manager->userInfoForAccountID(m_accountId)) {
            getUserInfoFinished(score); return;
        }
        auto const now = std::chrono::steady_clock::now();
        if (m_lookupFailed) return;
        if (now >= m_lookupDeadline) { m_lookupFailed = true; releaseDelegate(); render(); return; }
        if (m_inFlight && now - m_requestedAt < std::chrono::seconds(8)) return;
        if (m_inFlight) releaseDelegate();
        if (m_requests >= 3) { m_lookupFailed = true; render(); return; }
        if (now < m_nextRequest) return;
        // Share GD's native cache and serialize with PlayerIcon/ProfilePage.
        // Never take over another UserInfoDelegate's in-flight request.
        if (manager->m_userInfoDelegate && manager->m_userInfoDelegate != this) return;
        manager->m_userInfoDelegate = this;
        m_inFlight = true; ++m_requests; m_requestedAt = now;
        manager->getGJUserInfo(m_accountId);
    }
    // Q2 stat tile (.panel #20232E): the caption over the 24 px value.
    void tile(std::string const& caption, std::string const& value, int index, ccColor3B color = Text) {
        constexpr float width = 69.6f, gap = 6.0f, y = 96.0f;
        auto const x = 14.0f + width / 2.0f + static_cast<float>(index) * (width + gap);
        m_contents->addChild(surface({width, 40}, {x, y}, SurfaceRaised, SurfaceRaised, 255, false), 1);
        auto* text = menuLabel(caption, 13.f / 64.f, {x - width / 2.0f + 7.0f, y + 9.0f}, TextMuted, FontWeight::SemiBold);
        text->setAnchorPoint({0, 0.5f}); fitMenuLabelWidth(text, width - 12.0f, 13.f / 64.f, 0.09f); m_contents->addChild(text, 5);
        auto* number = menuLabel(value, 24.f / 64.f, {x - width / 2.0f + 7.0f, y - 6.0f}, color, FontWeight::Black);
        number->setAnchorPoint({0, 0.5f}); fitMenuLabelWidth(number, width - 12.0f, 24.f / 64.f, 0.12f); m_contents->addChild(number, 5);
    }
    void render() {
        if (m_contents) m_contents->removeFromParentAndCleanup(true);
        m_contents = CCNode::create(); m_panel->addChild(m_contents, 10);
        auto const& info = RankedRuntime::get().view().playerInfo;
        // Q2 board: PLAYER INFO, the 36 px name with the title and record,
        // the two mode rows, five stat tiles and the forum line.
        auto* title = menuLabel(i18n::text("player-info", "PLAYER INFO"), 14.f / 64.f, {14, 252}, TextMuted, FontWeight::ExtraBold);
        title->setAnchorPoint({0, 0.5f}); fitMenuLabelWidth(title, 200, 14.f / 64.f, 0.1f); m_contents->addChild(title);
        auto const& match = RankedRuntime::get().view().match;
        auto const& profile=m_side=="A"?match.playerAProfile:match.playerBProfile;
        auto* name=menuLabel(info.username,36.f/64.f,{14,224},Text,FontWeight::Black);name->setAnchorPoint({0,.5f});
        fitSingleLineMenuLabel(name,230,24,36.f/64.f);m_contents->addChild(name);
        publicPlayerTitle(m_contents,info.publicProfile["equippedTitle"],{130,203},232,.25f);
        // Q2 tier panels: Solo with this match's record, then Survival.
        publicTierPanel(m_contents,i18n::text("solo-ranked","SOLO RANKED"),info.publicProfile["solo"],
            matchRecordText(profile.wins,profile.losses,i18n::isKorean()),{106.5f,162},{185,52});
        publicTierPanel(m_contents,i18n::text("survival","SURVIVAL"),info.publicProfile["survival"],"",{293.5f,162},{185,52});
        auto const unavailable = i18n::text("info-unavailable", "Unavailable");
        auto const waiting = m_lookupFailed ? unavailable : i18n::text("loading", "Loading...");
        tile(i18n::text("info-stars", "Stars"), m_statistics ? fmt::format("{}", m_statistics->stars) : waiting, 0);
        tile(i18n::text("info-moons", "Moons"), m_statistics ? fmt::format("{}", m_statistics->moons) : waiting, 1);
        tile(i18n::text("info-demons", "Demons"), m_statistics ? fmt::format("{}", m_statistics->demons) : waiting, 2);
        auto const extreme = m_statistics && m_statistics->extreme ? fmt::format("{}", *m_statistics->extreme) : (m_statistics ? unavailable : waiting);
        auto const ranked = m_statistics && m_statistics->globalRank > 0;
        if (ranked) tile("GD Global Rank", fmt::format("#{}", m_statistics->globalRank), 3, Gold);
        else tile("Extreme Demons", extreme, 3);
        std::string footer;
        for (auto const& provider : {std::string("GDKF"), std::string("POINTERCRATE")}) {
            std::string value = info.forum.loading ? i18n::text("loading", "Loading...") : unavailable;
            ccColor3B color = TextMuted;
            for (auto const& record : info.forum.records) if (record.provider == provider) {
                if (record.rank) { value = fmt::format("#{}", *record.rank); color = Gold; }
                else if (record.status == "UNRANKED") value = i18n::text("info-unranked", "Not ranked");
                else if (record.status == "UNLINKED") value = i18n::text("info-unlinked", "Not linked");
                else if (record.status == "PENDING") value = i18n::text("loading", "Loading...");
                if (record.status == "ERROR" && record.rank) value += " *";
            }
            if (provider == "POINTERCRATE") tile("Pointercrate", value, 4, color);
            else footer = "GDKF " + value;
        }
        if (ranked) footer += " · Extreme Demons " + extreme;
        auto* line = menuLabel(footer, 13.f / 64.f, {14, 54}, TextMuted, FontWeight::SemiBold);
        line->setAnchorPoint({0, 0.5f}); fitMenuLabelWidth(line, 270, 13.f / 64.f, 0.09f); m_contents->addChild(line);
        if (info.botReference) {
            auto* hint = menuLabel(i18n::text("info-bot-reference", "Bot reference: CoJong"), 13.f / 64.f,
                {14, 20}, TextMuted); hint->setAnchorPoint({0, 0.5f}); fitMenuLabelWidth(hint, 260, 13.f / 64.f, 0.09f); m_contents->addChild(hint);
        }
    }
    void onGdProfile(CCObject*) {
        if (m_closed || m_accountId <= 0 || RankedRuntime::get().view().playerInfo.botReference) return;
        if (auto* page = ProfilePage::create(m_accountId, false)) page->show();
    }
    void refresh(float) {
        if (m_closed) return;
        auto const& view = RankedRuntime::get().view();
        if (view.stage != RuntimeStage::Matched || view.match.matchId != m_matchId ||
            view.playerInfo.side != m_side || view.match.state == "ROUND_ENTERING" ||
            view.match.state == "DEATHMATCH_ENTERING" || view.match.state == "ROUND_PLAYING" ||
            view.match.state == "DEATHMATCH_PLAYING") { dismiss(); return; }
        resolveStatistics();
        auto const now = std::chrono::steady_clock::now();
        auto const& forum = view.playerInfo.forum;
        if (forum.loaded && !forum.loading && m_forumReads < 12 && now >= m_nextForumRead &&
            std::any_of(forum.records.begin(), forum.records.end(), [](auto const& record) { return record.status == "PENDING"; })) {
            ++m_forumReads; m_nextForumRead = now + std::chrono::seconds(1);
            // Poll only the database while the one-time creation job finishes.
            // This endpoint never performs an external API refresh.
            RankedRuntime::get().fetchPlayerInfo(m_side);
        }
        if (m_revision != view.revision) {
            m_revision = view.revision; render();
        }
    }
public:
    static RankedPlayerInfoPopup* create(std::string side) {
        auto* result = new RankedPlayerInfoPopup;
        if (result->init(std::move(side))) { result->autorelease(); return result; }
        delete result; return nullptr;
    }
    ~RankedPlayerInfoPopup() override { releaseDelegate(); }
    void getUserInfoFinished(GJUserScore* score) override {
        if (m_closed || !score || score->m_accountID != m_accountId) return;
        m_statistics = Statistics {score->m_globalRank, score->m_stars, score->m_moons, score->m_demons,
            extremeDemonCount(std::string_view(score->m_demonInfo.data(), score->m_demonInfo.size()))};
        releaseDelegate(); render();
    }
    void getUserInfoFailed(int accountId) override {
        if (accountId != m_accountId) return;
        releaseDelegate(); m_nextRequest = std::chrono::steady_clock::now() + std::chrono::seconds(1); render();
    }
    void userInfoChanged(GJUserScore* score) override { getUserInfoFinished(score); }
    void onClose(CCObject*) override { dismiss(); }
    void dismiss() {
        if (m_closed) return;
        [[maybe_unused]] Ref<RankedPlayerInfoPopup> keepAlive(this);
        m_closed = true; releaseDelegate(); unschedule(schedule_selector(RankedPlayerInfoPopup::refresh));
        Popup::onClose(nullptr);
    }
    void onExit() override {
        m_closed = true; releaseDelegate(); unschedule(schedule_selector(RankedPlayerInfoPopup::refresh));
        if (!m_exited) { m_exited = true; CCTouchDispatcher::get()->unregisterForcePrio(this); }
        Popup::onExit();
    }
};
} // namespace corum::ranked::ui
