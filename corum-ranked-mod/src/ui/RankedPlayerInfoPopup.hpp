#pragma once

#include "../RankedRuntime.hpp"
#include "../domain/PlayerInfoPresentation.hpp"
#include "RankedTranslations.hpp"
#include "RankedVisuals.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
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
        if (!Popup::init(350, 285)) return false;
        m_bgSprite->setVisible(false); m_closeBtn->setVisible(false);
        setID("ranked-player-info-popup"_spr);
        m_side = std::move(side); m_matchId = RankedRuntime::get().view().match.matchId;
        RankedRuntime::get().fetchPlayerInfo(m_side);
        m_accountId = RankedRuntime::get().view().playerInfo.accountId;
        if (m_accountId <= 0) return false;
        m_lookupDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
        m_panel = surface({350, 285}, {175, 142.5f}, Cyan, Surface, 255, true);
        m_mainLayer->addChild(m_panel, 10);
        auto* menu = CCMenu::create(); menu->setPosition(CCPointZero); m_panel->addChild(menu, 30);
        auto* face = surface({130, 29}, {}, Blue, SurfaceRaised, 250, true);
        auto* caption = label(i18n::text("close", "Close"), 0.28f, {65, 14.5f}, Text, FontWeight::ExtraBold);
        face->addChild(caption);
        auto* close = CCMenuItemSpriteExtra::create(face, this, menu_selector(RankedPlayerInfoPopup::onClose));
        close->setPosition({175, 23}); menu->addChild(close);
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
    void row(std::string const& caption, std::string const& value, float y, ccColor3B color = Text) {
        auto* text = label(caption, 0.25f, {23, y}, TextMuted, FontWeight::SemiBold);
        text->setAnchorPoint({0, 0.5f}); fitLabelWidth(text, 190, 0.25f, 0.16f); m_contents->addChild(text);
        auto* number = label(value, 0.29f, {327, y}, color, FontWeight::ExtraBold);
        number->setAnchorPoint({1, 0.5f}); fitLabelWidth(number, 113, 0.29f, 0.16f); m_contents->addChild(number);
    }
    void render() {
        if (m_contents) m_contents->removeFromParentAndCleanup(true);
        m_contents = CCNode::create(); m_panel->addChild(m_contents, 10);
        auto const& info = RankedRuntime::get().view().playerInfo;
        auto* title = label(i18n::text("player-info", "PLAYER INFO"), 0.46f, {175, 257}, Text, FontWeight::Black);
        fitLabelWidth(title, 306, 0.46f, 0.28f); m_contents->addChild(title);
        auto* name = label(info.username, 0.35f, {175, 229}, Gold, FontWeight::ExtraBold);
        fitLabelWidth(name, 300, 0.35f, 0.20f); m_contents->addChild(name);
        float y = 202;
        if (m_statistics && m_statistics->globalRank > 0) {
            row("GD Global Rank", fmt::format("#{}", m_statistics->globalRank), y, Gold); y -= 22;
        }
        auto const unavailable = i18n::text("info-unavailable", "Unavailable");
        auto const waiting = m_lookupFailed ? unavailable : i18n::text("loading", "Loading...");
        row(i18n::text("info-stars", "Stars"), m_statistics ? fmt::format("{}", m_statistics->stars) : waiting, y); y -= 22;
        row(i18n::text("info-moons", "Moons"), m_statistics ? fmt::format("{}", m_statistics->moons) : waiting, y); y -= 22;
        row(i18n::text("info-demons", "Demons"), m_statistics ? fmt::format("{}", m_statistics->demons) : waiting, y); y -= 22;
        row("Extreme Demons", m_statistics && m_statistics->extreme ? fmt::format("{}", *m_statistics->extreme) :
            (m_statistics ? unavailable : waiting), y); y -= 25;
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
            row(provider == "GDKF" ? "GDKF" : "Pointercrate", value, y, color); y -= 22;
        }
        if (info.botReference) {
            auto* hint = label(i18n::text("info-bot-reference", "Bot reference: CoJong"), 0.18f,
                {175, 49}, TextMuted); fitLabelWidth(hint, 310, 0.18f, 0.13f); m_contents->addChild(hint);
        }
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
