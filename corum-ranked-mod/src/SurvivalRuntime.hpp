#pragma once
#include "domain/LevelPreparationPolicy.hpp"
#include "domain/ServerClock.hpp"
#include "domain/SurvivalMapPolicy.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include "domain/SurvivalFreePlayPolicy.hpp"
#include "domain/SurvivalFlowPolicy.hpp"
#include "domain/SurvivalEntryHold.hpp"
#include "domain/ResultScorePresentation.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <deque>

class PlayLayer;
namespace corum::ranked {
struct SurvivalQueueView {
    std::string status="LEFT",matchId,error,waitingSince;
    int players=0,minimumPlayers=8,maximumPlayers=16,maxWaitSeconds=180;
    bool enabled=false;
};
struct SurvivalMatchView {
    std::uint64_t revision=0;
    std::string id,state,phase,roundKey,group,ownId="human",error,startsAt,endsAt,entryOpensAt,entryRequestedAt,mapError,presentationGate,selectionOpensAt,eliminatedAt;
    int mapGeneration=0,roundNumber=0,targetQualifiers=0,playerCount=0,roundPlayerCount=0,matchPlayerCount=16;
    SurvivalMapMetadata map;
    matjson::Value certificate=nullptr,result=nullptr,completedRound=nullptr,nextMap=nullptr,participants=nullptr;
    std::vector<SurvivalStanding> standings;
    std::vector<SurvivalStanding> roundPlayers;
    std::vector<std::string> aliveIds;
    std::optional<double> cutoff,first;
    bool ownEntered=false,ownAssetsReady=false,ownEliminated=false,ownAbsent=false,resultPending=false,debug=false,online=false;
    bool ownWinner=false;
    std::string activeAttemptId;
    double activeAttemptProgress=0;
};

class SurvivalRuntime final : public cocos2d::CCNode {
public:
    static SurvivalRuntime& get();
    void update(float dt) override;
    void tick();
    void recover();
    void refreshRound() { m_nextPoll={}; poll(); }
    std::int64_t serverSyncMillis() const { return m_lastServerNow; }
    bool active() const { return !m_view.id.empty(); }
    bool creating() const { return m_creating; }
    bool queueing() const { return m_queue.status=="QUEUED" || m_queue.status=="JOINING"; }
    SurvivalQueueView const& queue() const { return m_queue; }
    int queueWaitingSeconds() const;
    int configuredPlayerCount() const;
    void joinQueue();
    void leaveQueue();
    std::string surrenderMessage() const;
    bool busy() const { return m_eventBusy; }
    SurvivalMatchView const& view() const { return m_view; }
    LevelPreparationContext preparationContext() const;
    std::string optionsContext() const {return active()?m_view.id+":"+m_view.roundKey+":"+std::to_string(m_view.mapGeneration):std::string{};}

    SurvivalMatchView preparationView() const;
    SurvivalOutcomeChoice outcomeChoice() const;
    SurvivalFlowStage flowStage() const{return survivalFlowStage(m_view.state,m_view.presentationGate,m_view.ownEliminated,outcomeChoice());}
    void chooseOutcome(SurvivalOutcomeChoice);
    bool observing() const{return m_view.ownEliminated && outcomeChoice()==SurvivalOutcomeChoice::Observe;}
    int watchLockSeconds() const;
    bool canEnterObserver() const;
    bool armObserver(GJGameLevel*,cocos2d::CCScene*);
    bool canReplayCleared() const;
    bool armClearedReplay(GJGameLevel*,cocos2d::CCScene*);
    void leaveObserver();
    bool previouslyEntered() const;
    void noteNativeLaunch();
    void observerReturned();
    bool acquiring() const;
    bool validateNative(GJGameLevel* level);
    bool canEnter() const;
    bool gameplayLevel(int id) const;
    bool armNative(GJGameLevel* level);
    bool consumeNativeLaunch(GJGameLevel* level);
    bool canPlay() const;
    bool attemptLimitReached() const;
    std::optional<std::int64_t> countdownMs() const;
    std::optional<std::int64_t> entryWaitMs() const;
    void selectedPanelReady(bool ready);
    std::optional<std::int64_t> selectedPanelWaitMs() const;
    std::optional<std::int64_t> roundRemainingMs() const;
    std::int64_t remainingMs() const;
    void assetsReady(GJGameLevel* level);
    void preparationFailed(std::string const& reason);
    void sceneEntered(int levelId);
    bool beginAttempt();
    void progress(double value);
    void endAttempt(double value);
    bool normalRoundCleared() const;
    bool canFreePlay() const;
    bool beginFreePlay();
    bool freePlaying() const {return m_freePlay.active;}
    bool freePlayReturnPending() const;
    void requestDirectFreePlayReturn(PlayLayer*);
    bool beginFreePlayNativeQuit(GJGameLevel* level);
    bool interceptFreePlayNativeQuit(GJGameLevel* level) const;
    void finishFreePlayNativeQuit();
    bool canSurrender() const;
    void surrender();
    bool surrenderPending() const {return !m_surrenderEventId.empty() && !m_surrenderAccepted;}
    void presentationComplete();
    void acknowledge();
    std::optional<ScoreAnimationInput> scoreAnimation() const;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void start(std::string password,std::string group,int forceRank=0,int botCount=-1);
    void forceRank(std::string password,int rank);
    void fillBots(std::string password,int botCount=-1);
    bool canForceClear() const;
    bool forceClearPending() const {return m_forceClearBody.has_value();}
    void forceClear();
    bool canEndRound() const;
    void endRound();
#endif
private:
    void pumpSurrenderReturn();
    void resetSurrender();
    std::string m_surrenderEventId,m_surrenderMatchId;
    bool m_surrenderAccepted=false;
    std::optional<geode::WeakRef<PlayLayer>> m_surrenderOwner;
    std::optional<geode::WeakRef<cocos2d::CCScene>> m_surrenderScene;
    void pumpFreePlayReturn();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    void pumpForceClear();
    // matjson::Value() is {}, not null. Request absence must be explicit;
    // isObject() otherwise blocks both Pause actions even before submission.
    std::optional<matjson::Value> m_forceClearBody;
    std::chrono::steady_clock::time_point m_forceClearRetryAt{};
    bool m_forceClearBusy=false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_forceClearRequest;
#endif
    SurvivalFreePlayState m_freePlay;
    std::string m_selectedPanelContext;
    mutable SurvivalEntryHold m_entryHold;
    std::optional<geode::WeakRef<cocos2d::CCScene>> m_freePlayOrigin,m_freePlayQuitScene;
    geode::Ref<GJGameLevel> m_freePlayQuitLevel;
    bool m_freePlayReturnQueued=false,m_freePlayNativeQuitFinished=false;
    void observe(matjson::Value const& root);
    void observeQueue(matjson::Value const& root);
    void pollQueue();
    void loadOnlineMatch(std::string const& id);
    void recoverLegacyDebug();
    void queueAction(std::string const& path,matjson::Value body);
    std::string matchEndpoint(std::string const& path) const;
    matjson::Value event(std::string const& type) const;
    void enqueue(matjson::Value body);
    void pumpEvents();
    void discardObsoleteEvents();
    void poll();
    bool current(std::uint64_t generation,std::string const& session) const;
    SurvivalMatchView m_view;
    SurvivalQueueView m_queue;
    ServerClock m_clock;
    LevelPreparationContext m_prepared;
    geode::Ref<GJGameLevel> m_launchLevel;
    std::string m_session,m_attempt,m_attemptContext,m_enteredContext,m_readyContext,m_lastMapDiagnostic,m_presentationContext;
    double m_attemptBest=0,m_lastProgress=-1;
    int m_localAttempts=0;
    float m_tick=0;
    bool m_creating=false,m_pollBusy=false,m_eventBusy=false,m_recoverBusy=false,m_needRecovery=false,m_queuePollBusy=false;
    std::uint64_t m_generation=0,m_queueGeneration=0,m_eventGeneration=0,m_pollGeneration=0;
    std::int64_t m_lastServerNow=0;
    std::deque<matjson::Value> m_events;
    std::chrono::steady_clock::time_point m_pollStartedAt{},m_nextPoll{},m_nextProgress{},m_retryAt{},m_recoverRetryAt{},m_nextQueuePoll{},m_ackRetryAt{};
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_controlRequest,m_pollRequest,m_eventRequest,m_recoverRequest,m_queueRequest;
};
void showSurvivalGame();
void showSurvivalQueue();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
void showSurvivalDebugPopup();
void showSurvivalForceRankPopup();
void showSurvivalFillBotsPopup();
#endif
}
