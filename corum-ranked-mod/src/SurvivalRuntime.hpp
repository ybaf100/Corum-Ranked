#pragma once
#include "domain/LevelPreparationPolicy.hpp"
#include "domain/ServerClock.hpp"
#include "domain/SurvivalMapPolicy.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include "domain/ResultScorePresentation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <deque>

namespace corum::ranked {
struct SurvivalMatchView {
    std::uint64_t revision=0;
    std::string id,state,phase,roundKey,group,ownId="human",error,startsAt,endsAt,mapError,presentationGate;
    int mapGeneration=0,roundNumber=0,targetQualifiers=0;
    SurvivalMapMetadata map;
    matjson::Value certificate,result,completedRound;
    std::vector<SurvivalStanding> standings;
    std::vector<std::string> aliveIds;
    std::optional<double> cutoff,first;
    bool ownEntered=false,ownAssetsReady=false,ownEliminated=false,debug=false;
    std::string activeAttemptId;
    double activeAttemptProgress=0;
};

class SurvivalRuntime final : public cocos2d::CCNode {
public:
    static SurvivalRuntime& get();
    void update(float dt) override;
    void tick();
    void recover();
    bool active() const { return !m_view.id.empty(); }
    bool creating() const { return m_creating; }
    bool busy() const { return m_eventBusy; }
    SurvivalMatchView const& view() const { return m_view; }
    LevelPreparationContext preparationContext() const;
    bool acquiring() const;
    bool validateNative(GJGameLevel* level);
    bool canEnter() const;
    bool gameplayLevel(int id) const;
    bool armNative(GJGameLevel* level);
    bool consumeNativeLaunch(GJGameLevel* level);
    bool canPlay() const;
    bool attemptLimitReached() const;
    std::optional<std::int64_t> countdownMs() const;
    std::int64_t remainingMs() const;
    void assetsReady(GJGameLevel* level);
    void preparationFailed(std::string const& reason);
    void sceneEntered(int levelId);
    bool beginAttempt();
    void progress(double value);
    void endAttempt(double value);
    void surrender();
    void presentationComplete();
    void acknowledge();
    std::optional<ScoreAnimationInput> scoreAnimation() const;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void start(std::string password,std::string group,int forceRank=0);
    void forceRank(std::string password,int rank);
#endif
private:
    void observe(matjson::Value const& root);
    matjson::Value event(std::string const& type) const;
    void enqueue(matjson::Value body);
    void pumpEvents();
    void poll();
    bool current(std::uint64_t generation,std::string const& session) const;
    SurvivalMatchView m_view;
    ServerClock m_clock;
    LevelPreparationContext m_prepared;
    geode::Ref<GJGameLevel> m_launchLevel;
    std::string m_session,m_attempt,m_attemptContext,m_enteredContext,m_readyContext,m_lastMapDiagnostic,m_presentationContext;
    double m_attemptBest=0,m_lastProgress=-1;
    int m_localAttempts=0;
    float m_tick=0;
    bool m_creating=false,m_pollBusy=false,m_eventBusy=false,m_recoverBusy=false,m_needRecovery=false;
    std::uint64_t m_generation=0;
    std::deque<matjson::Value> m_events;
    std::chrono::steady_clock::time_point m_nextPoll{},m_nextProgress{},m_retryAt{},m_recoverRetryAt{};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_controlRequest,m_pollRequest,m_eventRequest,m_recoverRequest;
};
void showSurvivalGame();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
void showSurvivalDebugPopup();
void showSurvivalForceRankPopup();
#endif
}
