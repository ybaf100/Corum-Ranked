#include <Geode/binding/GJAccountManager.hpp>
#include "domain/SurvivalBadgePresentation.hpp"
#include "domain/SurvivalRoundPresentation.hpp"
#include "replay/SurvivalReplayRecorder.hpp"
#include "SurvivalSpectatorRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "RankedRuntime.hpp"
#include "RankedAudioManager.hpp"
#include "RankedLevelPreparation.hpp"
#include "RankedRoundPreparation.hpp"
#include "SurvivalMapValidation.hpp"
#include "SurvivalSceneBridge.hpp"
#include <Geode/binding/MenuLayer.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include "ui/RankedTranslations.hpp"
#include "domain/Sha256.hpp"
#include "domain/ModUpdatePolicy.hpp"
#include <Geode/utils/random.hpp>
#include <Geode/cocos/support/zip_support/ZipUtils.h>

using namespace geode::prelude;
namespace corum::ranked {
namespace {
std::int64_t localNow() { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }
std::string onlineEndpoint(std::string const& path) { return "https://api.52-74-37-130.sslip.io/api/ranked/survival/online"+path; }
std::string queueEndpoint(std::string const& path) { return "https://api.52-74-37-130.sslip.io/api/ranked/survival/queue"+path; }
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
std::string debugEndpoint(std::string const& path) { return "https://api.52-74-37-130.sslip.io/api/ranked/survival/debug"+path; }
#endif
void openRecoveredGame() {
    queueInMainThread([] {
        auto* scene=CCDirector::sharedDirector()->getRunningScene();
        if(scene && scene->getChildByID("survival-online-queue"_spr))return;
        if(RankedRuntime::get().view().stage==RuntimeStage::Ready && SurvivalRuntime::get().active())showSurvivalGame();
    });
}
web::WebRequest request(std::string const& session) {
    web::WebRequest http;http.header("Authorization","Bearer "+session);http.followRedirects(false);http.timeout(std::chrono::seconds(10));return http;
}
SurvivalMapPhase mapPhase(std::string const& value) {
    return value=="ROUND_1"?SurvivalMapPhase::Round1:value=="ROUND_2"?SurvivalMapPhase::Round2:value=="ROUND_3"?SurvivalMapPhase::Round3:value=="ROUND_4"?SurvivalMapPhase::Round4:
        value=="FINAL"?SurvivalMapPhase::Final:SurvivalMapPhase::Tiebreak;
}
SurvivalDemon demon(std::string const& value) {
    return value=="EASY"?SurvivalDemon::Easy:value=="MEDIUM"?SurvivalDemon::Medium:value=="HARD"?SurvivalDemon::Hard:
        value=="INSANE"?SurvivalDemon::Insane:value=="EXTREME"?SurvivalDemon::Extreme:SurvivalDemon::None;
}
}
int SurvivalRuntime::configuredPlayerCount() const {return queueing()?m_queue.maximumPlayers:RankedRuntime::get().view().survivalPlayerCount;}
SurvivalRuntime& SurvivalRuntime::get() {
    static auto* instance=[] {auto* node=new SurvivalRuntime;node->init();
        CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(node,1,false);node->release();return node;}();return *instance;
}
bool SurvivalRuntime::current(std::uint64_t generation,std::string const& session) const {
    return generation==m_generation && session==m_session && session==RankedRuntime::get().spectatorLiveSessionToken();
}
void SurvivalRuntime::update(float dt) { m_tick-=dt;if(m_tick>0)return;
m_tick=.1f;tick(); }
void SurvivalRuntime::tick() {
    replay::Recorder::get().pump();
    auto const token=RankedRuntime::get().spectatorLiveSessionToken();
    if(token!=m_session) {
        replay::Recorder::get().finishFromLast("CONNECTION_LOST");
        ++m_generation;m_controlRequest.cancel();m_pollRequest.cancel();m_eventRequest.cancel();m_recoverRequest.cancel();m_queueRequest.cancel();
        m_lastServerNow=0;m_session=token;m_creating=m_pollBusy=m_eventBusy=m_recoverBusy=m_queuePollBusy=false;m_view={};m_queue={};++m_queueGeneration;m_prepared={};m_events.clear();m_attempt.clear();m_spectatorAcceptedAttempt.clear();
        m_attemptContext=m_enteredContext=m_readyContext=m_failedContext="";m_audioReported.clear();m_nextPoll={};
        m_clock={};resetSurrender();
        m_needRecovery=!token.empty();m_recoverRetryAt={};m_ackRetryAt={};
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
        m_forceClearRequest.cancel();m_forceClearBody.reset();m_forceClearBusy=false;
#endif
        if(m_needRecovery)recover();
    }
    pumpFreePlayReturn();
    SurvivalSpectatorRuntime::get().tick();
    pumpSurrenderReturn();
    if(token.empty())return;
    RankedRuntime::get().updateSessionHeartbeat();
    if(!active()) {
        if(m_queue.status=="MATCHED" && !m_queue.matchId.empty())loadOnlineMatch(m_queue.matchId);
        else if(queueing() && std::chrono::steady_clock::now()>=m_nextQueuePoll)pollQueue();
        else if(m_needRecovery && std::chrono::steady_clock::now()>=m_recoverRetryAt)recover();
        return;
    }
    if(acquiring())RankedRoundPreparation::get().pump();
    pumpEvents();
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    pumpForceClear();
#endif
    auto const now=std::chrono::steady_clock::now();
    if(m_pollBusy && now-m_pollStartedAt>std::chrono::seconds(8)){
        // A dropped HTTP completion must not hold the read-only match poll
        // indefinitely. Invalidate any late callback before retrying.
        log::warn("[SurvivalPoll] timed out; retrying match status");
        ++m_pollGeneration;m_pollRequest.cancel();m_pollBusy=false;
        m_nextPoll=now+std::chrono::milliseconds(500);
        m_view.error="Survival connection stalled. Retrying...";
    }
    if(now>=m_nextPoll)poll();
}
std::string SurvivalRuntime::matchEndpoint(std::string const& path) const {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    if(m_view.debug && !m_view.online)return debugEndpoint(path);
#endif
    return onlineEndpoint(path);
}
void SurvivalRuntime::recover() {
    if(m_recoverBusy || m_creating || m_session.empty())return;
    m_needRecovery=true;m_recoverBusy=true;auto const generation=m_generation;auto const session=m_session;
    m_recoverRequest.spawn(request(session).get(onlineEndpoint("/active")),[this,generation,session](web::WebResponse response) {
        if(!current(generation,session))return;
        m_recoverBusy=false;
        if(response.code()<200 || response.code()>=300){m_recoverRetryAt=std::chrono::steady_clock::now()+std::chrono::seconds(2);return;}
        m_needRecovery=false;auto const root=response.json().unwrapOr(matjson::Value());
        if(root["match"].isObject()){observe(root["match"]);openRecoveredGame();return;}
        observeQueue(root["queue"]);
        if(queueing()){queueInMainThread([]{if(SurvivalRuntime::get().queueing() && RankedRuntime::get().view().stage==RuntimeStage::Ready)showSurvivalQueue();});return;}
        recoverLegacyDebug();
    });
}
void SurvivalRuntime::recoverLegacyDebug() {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    if(rankedBuildChannel()!="debug" || m_recoverBusy || m_creating || m_session.empty() || active() || queueing())return;
    m_recoverBusy=true;auto const generation=m_generation;auto const session=m_session;
    m_recoverRequest.spawn(request(session).get(debugEndpoint("/active")),[this,generation,session](web::WebResponse response) {
        if(!current(generation,session))return;
        m_recoverBusy=false;
        if(response.code()<200 || response.code()>=300){m_needRecovery=true;m_recoverRetryAt=std::chrono::steady_clock::now()+std::chrono::seconds(2);return;}
        auto const root=response.json().unwrapOr(matjson::Value());if(root["match"].isObject()){observe(root["match"]);openRecoveredGame();}
    });
#endif
}
int SurvivalRuntime::queueWaitingSeconds() const {
    auto const start=parseIso8601Millis(m_queue.waitingSince);
    return queueing() && start ? static_cast<int>(std::max<std::int64_t>(0,m_clock.serverNowMillis(localNow())-*start)/1000):0;
}
void SurvivalRuntime::observeQueue(matjson::Value const& root) {
    if(!root.isObject() || !root["status"].isString())return;
    auto const previousMatchId=m_queue.matchId;
    m_queue.status=root["status"].asString().unwrapOr("LEFT");m_queue.matchId=root["matchId"].asString().unwrapOr("");
    if(previousMatchId!=m_queue.matchId){
        // A poll for a prior queue match must not hold m_pollBusy forever
        // after the queue points at a replacement match.
        m_pollRequest.cancel();
        m_pollBusy=false;
        m_nextPoll={};
    }
    m_queue.players=static_cast<int>(root["players"].asInt().unwrapOr(m_queue.players));
    m_queue.minimumPlayers=static_cast<int>(root["minimumPlayers"].asInt().unwrapOr(8));m_queue.maximumPlayers=static_cast<int>(root["maximumPlayers"].asInt().unwrapOr(16));m_queue.maxWaitSeconds=static_cast<int>(root["maxWaitSeconds"].asInt().unwrapOr(180));
    m_queue.waitingSince=root["waitingSince"].asString().unwrapOr(m_queue.waitingSince);m_queue.enabled=root["enabled"].asBool().unwrapOr(m_queue.enabled);
    m_clock.observe(root["serverNow"].asString().unwrapOr(""),localNow());m_queue.error.clear();
    if(m_queue.status=="MATCHED" && !m_queue.matchId.empty())loadOnlineMatch(m_queue.matchId);
}
void SurvivalRuntime::loadOnlineMatch(std::string const& id) {
    if(active() || m_pollBusy || m_session.empty() || std::chrono::steady_clock::now()<m_nextPoll)return;
    m_pollBusy=true;auto const generation=m_generation;auto const session=m_session;
    m_pollRequest.spawn(request(session).get(onlineEndpoint("/matches/"+id)),[this,generation,session,id](web::WebResponse response) {
        if(!current(generation,session) || m_queue.matchId!=id)return;
        m_pollBusy=false;
        if(response.code()>=200&&response.code()<300){observe(response.json().unwrapOr(matjson::Value()));openRecoveredGame();}
        else {m_queue.error="Survival server connection failed. Retrying...";m_nextPoll=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
    });
}
void SurvivalRuntime::pollQueue() {
    if(m_queuePollBusy || m_creating || m_session.empty() || !queueing())return;
    m_queuePollBusy=true;auto const generation=m_generation,epoch=m_queueGeneration;auto const session=m_session;
    m_queueRequest.spawn(request(session).post(queueEndpoint("/heartbeat")),[this,generation,epoch,session](web::WebResponse response){
        if(!current(generation,session) || epoch!=m_queueGeneration)return;
        m_queuePollBusy=false;
        m_nextQueuePoll=std::chrono::steady_clock::now()+std::chrono::seconds(2);
        if(response.code()>=200&&response.code()<300)observeQueue(response.json().unwrapOr(matjson::Value()));
        else {m_queue.error="Survival server connection failed. Retrying...";m_nextQueuePoll=std::chrono::steady_clock::now()+std::chrono::seconds(2);}
    });
}
void SurvivalRuntime::queueAction(std::string const& path,matjson::Value body) {
    if(m_creating || m_session.empty() || active())return;
    ++m_queueGeneration;m_queueRequest.cancel();m_queuePollBusy=false;m_creating=true;m_recoverRequest.cancel();m_recoverBusy=false;m_needRecovery=false;
    auto http=request(m_session);http.bodyJSON(body);auto const generation=m_generation,epoch=m_queueGeneration;auto const session=m_session;
    m_controlRequest.spawn(http.post(queueEndpoint(path)),[this,generation,epoch,session,path](web::WebResponse response){
        if(!current(generation,session) || epoch!=m_queueGeneration)return;
        m_creating=false;
        auto const root=response.json().unwrapOr(matjson::Value());
        if(response.code()>=200&&response.code()<300){observeQueue(root);return;}
        if(RankedRuntime::get().handleSurvivalAdmissionError(response))return;
        m_queue.error=root["message"].asString().unwrapOr("Survival server connection failed. Retrying...");
        if(path=="/join")m_queue.status="LEFT";
        m_needRecovery=response.code()==0 || response.code()>=500 || response.code()==409;m_recoverRetryAt={};m_nextQueuePoll={};
    });
}
void SurvivalRuntime::joinQueue() {
    tick();if(active()){showSurvivalGame();return;}if(queueing()){showSurvivalQueue();return;}
    matjson::Value body;if(m_creating || !RankedRuntime::get().view().survival.onlineAvailable || !RankedRuntime::get().prepareSurvivalAdmission(body))return;
    m_queue.status="JOINING";m_queue.error.clear();queueAction("/join",std::move(body));showSurvivalQueue();
}
void SurvivalRuntime::leaveQueue() { if(queueing() && !m_creating)queueAction("/leave",matjson::Value()); }
std::string SurvivalRuntime::surrenderMessage() const {
    if(!m_view.online)return i18n::text("survival-surrender-warning","End this match using this round's recorded ranking?");
    return fmt::format(fmt::runtime(i18n::text("survival-online-surrender","End as {} in this round, settle LP and return to the lobby?")),
        i18n::isKorean()?fmt::format(fmt::runtime(i18n::text("player-rank","{}")),m_view.roundPlayerCount):survivalOrdinal(m_view.roundPlayerCount));
}
void SurvivalRuntime::observe(matjson::Value const& root) {
    auto const id=root["matchId"].asString().unwrapOr("");auto const revision=root["revision"].asInt().unwrapOr(0);
    auto const serverTime=parseIso8601Millis(root["serverNow"].asString().unwrapOr(""));
    if(id==m_view.id && revision==static_cast<std::int64_t>(m_view.revision) && serverTime && *serverTime<m_lastServerNow)return;
    if(id.empty() || revision<1 || (!m_view.id.empty() && id!=m_view.id) || (id==m_view.id && revision<static_cast<std::int64_t>(m_view.revision)))return;
    if(m_view.id.empty()){m_lastServerNow=0;m_clock={};}
    auto const oldContext=preparationContext();
    auto const oldState=m_view.state;
    auto const watchedFinal=(m_view.phase=="FINAL"||m_view.phase=="TIEBREAK")&&SurvivalSpectatorRuntime::get().watchingThroughEnd();
    m_view.id=id;m_view.revision=static_cast<std::uint64_t>(revision);m_view.state=root["state"].asString().unwrapOr("");
    m_view.phase=root["phase"].asString().unwrapOr("");m_view.roundKey=root["roundKey"].asString().unwrapOr("");
    m_view.presentationGate=root["presentationGate"].asString().unwrapOr("");
    m_view.group=root["difficultyGroup"].asString().unwrapOr("");m_view.ownId=root["myParticipantId"].asString().unwrapOr("human");
    m_view.ownWinner=root["winnerId"].asString().unwrapOr("")==m_view.ownId;
    for(auto const& winner:root["winnerIds"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(winner.asString().unwrapOr("")==m_view.ownId)m_view.ownWinner=true;
    m_view.online=root["online"].asBool().unwrapOr(false);m_view.resultPending=root["myResultPending"].asBool().unwrapOr(false);
    m_view.debug=root["debug"].asBool().unwrapOr(false);m_view.mapGeneration=static_cast<int>(root["mapGeneration"].asInt().unwrapOr(0));
    m_view.roundNumber=static_cast<int>(root["displayRoundNumber"].asInt().unwrapOr(root["roundNumber"].asInt().unwrapOr(0)));
    m_view.tiebreakAttempts=static_cast<int>(root["tiebreakAttempts"].asInt().unwrapOr(3));
    if(m_view.tiebreakAttempts!=3 && m_view.tiebreakAttempts!=6)m_view.tiebreakAttempts=3;
    m_view.matchPlayerCount=root["matchPlayerCount"].asInt().unwrapOr(16)==64?64:16;
    m_view.playerCount=static_cast<int>(root["playerCount"].asInt().unwrapOr(16));m_view.roundPlayerCount=static_cast<int>(root["roundPlayerCount"].asInt().unwrapOr(16));
    m_view.targetQualifiers=static_cast<int>(root["targetQualifiers"].asInt().unwrapOr(0));
    m_view.startsAt=root["startsAt"].asString().unwrapOr("");m_view.endsAt=root["endsAt"].asString().unwrapOr("");
    m_view.entryOpensAt=root["entryOpensAt"].asString().unwrapOr("");
    m_view.entryRequestedAt=root["entryRequestedAt"].asString().unwrapOr("");
    m_view.certificate=root["map"];m_view.result=root["result"];
    m_view.roundBadges=root["roundBadges"];
    m_view.completedRound=root["completedRound"];m_view.nextMap=root["nextMap"];m_view.participants=root["participants"];
    if(oldState!="FINAL_RESULT"&&m_view.state=="FINAL_RESULT"&&watchedFinal)Mod::get()->setSavedValue(resultFlowKey()+":watched",true);
    m_view.selectionOpensAt=root["selectionOpensAt"].asString().unwrapOr("");
    m_view.mapError=root["mapSelection"]["errorCode"].asString().unwrapOr("");
    auto const diagnostic=m_view.id+":"+m_view.roundKey+":"+std::to_string(m_view.mapGeneration)+":"+m_view.mapError+
        ":"+root["mapSelection"]["errorMessage"].asString().unwrapOr("");
    if(!m_view.mapError.empty() && diagnostic!=m_lastMapDiagnostic) {
        m_lastMapDiagnostic=diagnostic;
        log::warn("[SurvivalMap] match={} round={} generation={} error={} detail={}",m_view.id,m_view.roundKey,m_view.mapGeneration,
            m_view.mapError,root["mapSelection"]["errorMessage"].asString().unwrapOr(""));
    }
    auto const& map=root["map"];m_view.map={static_cast<int>(map["levelId"].asInt().unwrapOr(0)),static_cast<int>(map["stars"].asInt().unwrapOr(0)),
        static_cast<int>(map["levelVersion"].asInt().unwrapOr(0)),map["demon"].asBool().unwrapOr(false)?demon(map["demonDifficulty"].asString().unwrapOr("")):SurvivalDemon::None,
        map["automatic"].asBool().unwrapOr(true),map["platformer"].asBool().unwrapOr(true),static_cast<int>(map["length"].asInt().unwrapOr(-1))};
    if(serverTime && *serverTime>=m_lastServerNow){m_lastServerNow=*serverTime;m_clock.observe(root["serverNow"].asString().unwrapOr(""),localNow());}
    m_view.cutoff=root["cutoffScore"].isNumber()?std::optional(root["cutoffScore"].asDouble().unwrapOr(0)):std::nullopt;
    m_view.first=root["finalFirstScore"].isNumber()?std::optional(root["finalFirstScore"].asDouble().unwrapOr(0)):std::nullopt;
    m_view.standings.clear();auto participants=root["participants"].asArray().unwrapOr(std::vector<matjson::Value>{});
    m_view.aliveIds.clear();
    for(auto const& p:participants) {
        auto const pid=p["participantId"].asString().unwrapOr("");
        if(p["eliminationStage"].isNull())m_view.aliveIds.push_back(pid);
        if(pid==m_view.ownId){m_view.ownEliminated=!p["eliminationStage"].isNull();m_view.eliminatedAt=p["eliminatedAt"].asString().unwrapOr("");
            if(m_view.ownEliminated && m_view.eliminatedAt.empty()){
                auto const key="survival-elimination-time-"+id;
                if(!Mod::get()->getSavedValue<std::int64_t>(key,0))Mod::get()->setSavedValue(key,m_lastServerNow);
            }
        }
    }
    for(auto const& p:root["standings"].asArray().unwrapOr(std::vector<matjson::Value>{})) {
        auto const pid=p["participantId"].asString().unwrapOr("");
        auto info=std::find_if(participants.begin(),participants.end(),[&](auto const& v){return v["participantId"].asString().unwrapOr("")==pid;});
        m_view.standings.push_back({pid,info==participants.end()?pid:(*info)["name"].asString().unwrapOr(pid),
            static_cast<int>(p["rank"].asInt().unwrapOr(0)),static_cast<int>(p["attemptsUsed"].asInt().unwrapOr(0)),static_cast<int>(p["attemptsFinished"].asInt().unwrapOr(0)),
            p["score"].asDouble().unwrapOr(0),p["passing"].asBool().unwrapOr(false),info!=participants.end()&&(*info)["kind"].asString().unwrapOr("")=="BOT",p["waitingForOthers"].asBool().unwrapOr(false),
            info!=participants.end()?(*info)["displayedTier"].asString().unwrapOr("UNRANKED"):"UNRANKED",
            info!=participants.end()?numFromString<int>((*info)["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0):0});
        auto& row=m_view.standings.back();row.bestProgress=p["bestProgress"].asDouble().unwrapOr(row.score);
        row.currentProgress=p["currentProgress"].asDouble().unwrapOr(0);row.attemptActive=p["attemptActive"].asBool().unwrapOr(false);
        row.attemptId=p["attemptId"].asString().unwrapOr("");
        row.cleared=p["cleared"].asBool().unwrapOr(false);row.eliminated=p["eliminated"].asBool().unwrapOr(false);row.qualified=p["qualified"].asBool().unwrapOr(false);
    }
    m_view.roundPlayers=m_view.standings;
    if(root["roundPlayers"].isArray()){
        m_view.roundPlayers.clear();
        for(auto const& p:root["roundPlayers"].asArray().unwrapOr(std::vector<matjson::Value>{})){
            auto const pid=p["participantId"].asString().unwrapOr("");
            auto existing=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& v){return v.id==pid;});
            SurvivalStanding row=existing==m_view.standings.end()?SurvivalStanding{}:*existing;
            auto info=std::find_if(participants.begin(),participants.end(),[&](auto const& v){return v["participantId"].asString().unwrapOr("")==pid;});
            row.id=pid;row.rank=static_cast<int>(p["rank"].asInt().unwrapOr(0));row.score=p["score"].asDouble().unwrapOr(0);
            row.bestProgress=p["bestProgress"].asDouble().unwrapOr(row.score);row.currentProgress=p["currentProgress"].asDouble().unwrapOr(0);
            row.attemptActive=p["attemptActive"].asBool().unwrapOr(false);row.attemptId=p["attemptId"].asString().unwrapOr("");
            row.cleared=p["cleared"].asBool().unwrapOr(false);
            row.eliminated=p["eliminated"].asBool().unwrapOr(false);row.qualified=p["qualified"].asBool().unwrapOr(false);
            if(info!=participants.end()){row.name=(*info)["name"].asString().unwrapOr(pid);row.bot=(*info)["kind"].asString().unwrapOr("")=="BOT";
                row.tier=(*info)["displayedTier"].asString().unwrapOr("UNRANKED");row.accountId=numFromString<int>((*info)["gdAccountId"].asString().unwrapOr("0")).unwrapOr(0);}
            m_view.roundPlayers.push_back(std::move(row));
        }
    }
    m_view.ownAssetsReady=m_view.ownEntered=m_view.ownAbsent=false;
    for(auto const& p:root["preparation"].asArray().unwrapOr(std::vector<matjson::Value>{}))if(p["participantId"].asString().unwrapOr("")==m_view.ownId) {
        m_view.ownAssetsReady=p["assetsReady"].asBool().unwrapOr(false);m_view.ownEntered=p["entered"].asBool().unwrapOr(false);m_view.ownAbsent=p["absent"].asBool().unwrapOr(false);
    }
    m_view.spectatorDemand=root["spectatorDemand"].asBool().unwrapOr(false);
    m_view.activeAttemptId=root["myActiveAttemptId"].asString().unwrapOr("");m_view.activeAttemptProgress=root["myActiveAttemptProgress"].asDouble().unwrapOr(0);
    if(oldContext!=preparationContext()) {
        replay::Recorder::get().finishFromLast("ROUND_TRANSITION");
        m_prepared={};m_attempt.clear();m_spectatorAcceptedAttempt.clear();m_attemptContext=m_enteredContext=m_readyContext=m_failedContext="";m_audioReported.clear();m_lastProgress=-1;m_localAttempts=0;
    }
    if(oldState=="PLAYING"&&m_view.state!="PLAYING")replay::Recorder::get().finishFromLast("SERVER_ENDED");
    pumpSurrenderReturn();
    discardObsoleteEvents();
    m_view.error.clear();Mod::get()->setSavedValue("survival-debug-match",id);
}
void SurvivalRuntime::poll() {
    if(m_pollBusy || !active() || m_session.empty())return;
    m_pollBusy=true;m_pollStartedAt=std::chrono::steady_clock::now();
    auto const generation=m_generation, pollGeneration=++m_pollGeneration;
    auto const session=m_session;auto const id=m_view.id;
    m_pollRequest.spawn(request(session).get(matchEndpoint("/matches/"+id)+(m_view.online?"?roundKey="+m_view.roundKey+"&mapGeneration="+std::to_string(m_view.mapGeneration):"")),[this,generation,pollGeneration,session,id](web::WebResponse response) {
        if(!current(generation,session) || m_view.id!=id || pollGeneration!=m_pollGeneration)return;
        m_pollBusy=false;m_nextPoll=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);
        if(response.code()>=200&&response.code()<300)observe(response.json().unwrapOr(matjson::Value()));
        else {
            auto const message=response.json().unwrapOr(matjson::Value())["message"].asString().unwrapOr("");
            if(m_view.online && response.code()==403 && message=="Not the online Survival participant"){
                ++m_generation;m_eventRequest.cancel();m_controlRequest.cancel();m_eventBusy=m_creating=false;
                m_view={};m_queue={};m_prepared={};m_events.clear();m_needRecovery=true;m_recoverRetryAt={};
                resetSurrender();m_clock={};m_lastServerNow=0;
            }else m_view.error="Survival server connection failed. Retrying...";
        }
    });
}
std::string SurvivalRuntime::resultFlowKey() const {return "survival-result-flow:"+std::to_string(GJAccountManager::sharedState()->m_accountID)+":"+m_view.id;}
void SurvivalRuntime::completeResultScreen(bool score){
    if(!m_view.result.isObject() || (m_view.state!="FINAL_RESULT"&&!m_view.ownEliminated))return;
    auto const key=resultFlowKey();auto const old=Mod::get()->getSavedValue<int>(key);
    Mod::get()->setSavedValue(key,std::max(old,score?2:1));(void)Mod::get()->saveData();
}
SurvivalFlowStage SurvivalRuntime::flowStage() const {
    if(m_view.state!="FINAL_RESULT" && outcomeChoice()!=SurvivalOutcomeChoice::PersonalResult && !(m_view.ownEliminated&&Mod::get()->getSavedValue<bool>(resultFlowKey()+":requested")))return survivalFlowStage(m_view.state,m_view.presentationGate,m_view.ownEliminated,outcomeChoice());
    bool podium=false;for(auto const& p:m_view.participants.asArray().unwrapOr(std::vector<matjson::Value>{}))
        if(p["participantId"].asString().unwrapOr("")==m_view.ownId){auto rank=p["finalRank"].asInt().unwrapOr(0);podium=m_view.state=="FINAL_RESULT"&&rank>=1&&rank<=3;}
    auto const screen=survivalResultScreen(m_view.result.isObject(),podium||Mod::get()->getSavedValue<bool>(resultFlowKey()+":watched"),Mod::get()->getSavedValue<int>(resultFlowKey()));
    return screen==SurvivalResultScreen::Ceremony?SurvivalFlowStage::Ceremony:screen==SurvivalResultScreen::Personal?SurvivalFlowStage::PersonalResult:SurvivalFlowStage::ScoreAnimation;
}
SurvivalOutcomeChoice SurvivalRuntime::outcomeChoice() const {
    if(Mod::get()->getSavedValue<std::string>("survival-outcome-context")!=optionsContext())return SurvivalOutcomeChoice::Undecided;
    auto const choice=Mod::get()->getSavedValue<int>("survival-outcome-choice",0);
    return choice==1&&m_view.ownEliminated?SurvivalOutcomeChoice::Observe:choice==2&&(m_view.state=="FINAL_RESULT"||(m_view.ownEliminated&&m_view.result.isObject()))?SurvivalOutcomeChoice::PersonalResult:choice==3&&officialRoundCleared()?SurvivalOutcomeChoice::LiveSpectate:choice==4&&m_view.ownEliminated?SurvivalOutcomeChoice::Standings:SurvivalOutcomeChoice::Undecided;
}
void SurvivalRuntime::chooseOutcome(SurvivalOutcomeChoice choice){
    if(choice==outcomeChoice()||!active())return;
    if(choice==SurvivalOutcomeChoice::LiveSpectate){if(!officialRoundCleared()||freePlaying())return;}
    else if(choice==SurvivalOutcomeChoice::PersonalResult&&m_view.state!="FINAL_RESULT"&&!(m_view.ownEliminated&&m_view.result.isObject()))return;
    else if(!m_view.ownEliminated){if(choice!=SurvivalOutcomeChoice::Undecided&&m_view.state!="FINAL_RESULT")return;}
    else if(choice==SurvivalOutcomeChoice::Observe&&watchLockSeconds()>0)return;
    Mod::get()->setSavedValue(resultFlowKey()+":requested",choice==SurvivalOutcomeChoice::PersonalResult);
    Mod::get()->setSavedValue("survival-outcome-context",optionsContext());Mod::get()->setSavedValue("survival-outcome-choice",static_cast<int>(choice));
    (void)Mod::get()->saveData();
}
int SurvivalRuntime::watchLockSeconds() const {
    if(!m_clock.synchronized())return 5;
    auto const at=parseIso8601Millis(m_view.eliminatedAt).value_or(Mod::get()->getSavedValue<std::int64_t>("survival-elimination-time-"+m_view.id,0));
    return at>0?survivalWatchLock(m_clock.serverNowMillis(localNow()),at):5;
}
void SurvivalRuntime::observerReturned(){
    if(!m_freePlay.active)return;
    auto origin=m_freePlayOrigin?m_freePlayOrigin->lock():Ref<CCScene>();
    if(origin && CCDirector::sharedDirector()->getRunningScene()==origin.data()){
        m_freePlay.finish();m_freePlayOrigin.reset();m_freePlayQuitLevel=nullptr;m_freePlayQuitScene.reset();m_freePlayNativeQuitFinished=false;
    }
}
void SurvivalRuntime::noteNativeLaunch(){Mod::get()->setSavedValue("survival-native-entered",optionsContext());}
bool SurvivalRuntime::previouslyEntered() const {
    return m_view.ownEntered || Mod::get()->getSavedValue<std::string>("survival-native-entered")==optionsContext();
}
SurvivalMatchView SurvivalRuntime::preparationView() const {
    auto view=m_view;
    if(view.state=="ROUND_RESULT" && view.nextMap["map"].isObject()){
        auto const& map=view.nextMap["map"];view.certificate=map;
        view.roundKey=view.nextMap["roundKey"].asString().unwrapOr("");view.phase=view.nextMap["phase"].asString().unwrapOr("");
        view.mapGeneration=static_cast<int>(view.nextMap["mapGeneration"].asInt().unwrapOr(0));
        view.roundNumber=static_cast<int>(view.nextMap["displayRoundNumber"].asInt().unwrapOr(view.roundNumber+1));
        view.map={static_cast<int>(map["levelId"].asInt().unwrapOr(0)),static_cast<int>(map["stars"].asInt().unwrapOr(0)),
            static_cast<int>(map["levelVersion"].asInt().unwrapOr(0)),map["demon"].asBool().unwrapOr(false)?demon(map["demonDifficulty"].asString().unwrapOr("")):SurvivalDemon::None,
            map["automatic"].asBool().unwrapOr(true),map["platformer"].asBool().unwrapOr(true),static_cast<int>(map["length"].asInt().unwrapOr(-1))};
    }
    return view;
}
LevelPreparationContext SurvivalRuntime::preparationContext() const {
    auto const view=preparationView();
    if(!active() || !view.map.levelId || (view.ownEliminated && !observing() && outcomeChoice()!=SurvivalOutcomeChoice::LiveSpectate) || view.state=="FINAL_RESULT" || view.state=="CANCELLED")return {};
    return {view.id,view.map.levelId,view.roundNumber,view.phase=="TIEBREAK"?1:0,false,0,view.mapGeneration};
}
bool SurvivalRuntime::acquiring() const { return preparationContext().valid(); }
bool SurvivalRuntime::validateNative(GJGameLevel* level) {
    auto const group=m_view.group=="RED_BRONZE"?SurvivalDifficultyGroup::RedBronze:m_view.group=="SILVER"?SurvivalDifficultyGroup::Silver:SurvivalDifficultyGroup::Gold;
    auto const context=preparationContext();auto const preparedView=preparationView();
    if(!context.valid() || !RankedLevelPreparation::get().authorizes(level,context) ||
        !survivalNativeMapMatches(level,preparedView.map,group,mapPhase(preparedView.phase)))return false;
    auto const expected=preparedView.certificate["geometrySha256"].asString().unwrapOr("");
    auto const bytes=preparedView.certificate["geometryBytes"].asInt().unwrapOr(0);
    if(expected.size()!=64 || bytes<1 || bytes>32*1024*1024 || level->m_levelString.size()>44*1024*1024)return false;
    std::string data(level->m_levelString.c_str());
    auto matches=[&](std::string const& value){return value.size()==static_cast<std::size_t>(bytes)&&sha256Text(value)==expected;};
    // Native Saved geometry can already be decompressed; otherwise use GD's
    // actual decoder, then verify the server's geometry byte count and SHA-256.
    if(!matches(data))data=std::string(ZipUtils::decompressString(level->m_levelString,false,11).c_str());
    if(!matches(data)) {m_view.error="Native Survival map data does not match the server.";return false;}
    m_prepared=context;return true;
}
bool SurvivalRuntime::gameplayLevel(int id) const { return id>0 && m_prepared==preparationContext() && m_prepared.levelId==id; }
bool SurvivalRuntime::armNative(GJGameLevel* level) {
    if(!canEnter() || !validateNative(level))return false;m_launchLevel=level;return true;
}
bool SurvivalRuntime::consumeNativeLaunch(GJGameLevel* level) {
    if(!m_launchLevel || level!=m_launchLevel.data() || !gameplayLevel(static_cast<int>(level->m_levelID)))return false;
    m_launchLevel=nullptr;return true;
}
bool SurvivalRuntime::canEnter() const {
    if(surrenderPending() || normalRoundCleared() || freePlaying())return false;
    if(m_view.state=="ENTERING"){
        auto const wait=entryWaitMs();if(!wait || *wait>0)return false;
    }
    if(m_view.state!="PLAYING" && !previouslyEntered()){auto const panelWait=selectedPanelWaitMs();if(!panelWait || *panelWait>0)return false;}
    if(m_view.state=="PLAYING" && remainingMs()<=0)return false;
    if(!m_prepared.valid() || m_prepared!=preparationContext() || m_session.empty())return false;
    if(m_view.online && !m_view.ownAssetsReady)return false;
    if(attemptLimitReached())return false;
    return !m_view.ownEliminated && (m_view.state=="ENTERING" || m_view.state=="COUNTDOWN" || m_view.state=="PLAYING");
}
bool SurvivalRuntime::canPlay() const {
    return !surrenderPending() && !normalRoundCleared() && !freePlaying() && m_view.state=="PLAYING" && !m_view.ownEliminated && !m_session.empty() &&
        m_session==RankedRuntime::get().spectatorLiveSessionToken() && remainingMs()>0 &&
        (!m_view.online || (m_view.ownAssetsReady && m_view.ownEntered && !m_view.ownAbsent));
}
bool SurvivalRuntime::attemptLimitReached() const {
    if(m_view.phase!="TIEBREAK")return false;
    auto own=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& p){return p.id==m_view.ownId;});
    return std::max(m_localAttempts,own==m_view.standings.end()?0:own->attemptsUsed)>=m_view.tiebreakAttempts;
}
bool SurvivalRuntime::exhaustedAttemptsAcknowledged() const {
    if(m_view.phase!="TIEBREAK" || freePlaying() || observing() || !m_attempt.empty())return false;
    auto own=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& p){return p.id==m_view.ownId;});
    // Started is not finished. Poll/ACK authority proves every terminal event,
    // so pending in-memory telemetry never needs to survive scene destruction.
    return own!=m_view.standings.end() && own->attemptsFinished>=m_view.tiebreakAttempts &&
        !own->attemptActive && m_view.activeAttemptId.empty() &&
        std::none_of(m_events.begin(),m_events.end(),[](auto const& e){return e["type"].asString().unwrapOr("")=="ATTEMPT_END";});
}
std::optional<std::int64_t> SurvivalRuntime::serverSyncAgeMs()const{
    if(!m_clock.synchronized()||m_lastServerNow<=0)return {};
    return std::max<std::int64_t>(0,m_clock.serverNowMillis(localNow())-m_lastServerNow);
}
std::optional<std::int64_t> SurvivalRuntime::countdownMs() const { return m_clock.signedRemainingMillis(m_view.startsAt,localNow()); }
std::optional<std::int64_t> SurvivalRuntime::entryWaitMs() const { return m_clock.signedRemainingMillis(m_view.entryOpensAt,localNow()); }
void SurvivalRuntime::selectedPanelReady(bool ready){
    if(ready && !m_view.ownEliminated)m_selectedPanelContext=optionsContext();
}
std::optional<std::int64_t> SurvivalRuntime::selectedPanelWaitMs() const {
    // A local presentation hold cannot restart an authoritative live round.
    if(m_view.state=="PLAYING")return 0;
    if(previouslyEntered())return entryWaitMs();
    auto const requested=parseIso8601Millis(m_view.entryRequestedAt),opens=parseIso8601Millis(m_view.entryOpensAt);
    auto const steady=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return m_entryHold.remaining(optionsContext(),requested.value_or(0),opens.value_or(0),m_clock.serverNowMillis(localNow()),
        m_selectedPanelContext==optionsContext(),m_clock.synchronized() &&
        (m_view.state=="ENTERING" || m_view.state=="COUNTDOWN" || m_view.state=="PLAYING") && m_view.ownAssetsReady,steady);
}
std::optional<std::int64_t> SurvivalRuntime::roundRemainingMs() const { return m_clock.remainingMillis(m_view.endsAt,localNow()); }
std::int64_t SurvivalRuntime::remainingMs() const { return roundRemainingMs().value_or(0); }
matjson::Value SurvivalRuntime::event(std::string const& type) const {
    matjson::Value body;body["eventId"]=utils::random::generateUUID();body["type"]=type;body["roundKey"]=m_view.roundKey;
    body["mapGeneration"]=m_view.mapGeneration;body["levelId"]=m_view.map.levelId;return body;
}
void SurvivalRuntime::enqueue(matjson::Value body) {
    if(!active() || m_session.empty())return;
    if(m_events.size()>256){m_view.error="Survival telemetry is pending. Reconnect to recover the game.";return;}
    m_events.push_back(std::move(body));pumpEvents();
}
void SurvivalRuntime::pumpEvents() {
    discardObsoleteEvents();
    if(m_eventBusy || m_events.empty() || m_session.empty() || std::chrono::steady_clock::now()<m_retryAt)return;
    m_eventBusy=true;auto const body=m_events.front();auto http=request(m_session);http.bodyJSON(body);
    auto const generation=m_generation;auto const session=m_session;auto const id=m_view.id;auto const eventId=body["eventId"].asString().unwrapOr("");
    auto const eventType=body["type"].asString().unwrapOr("");
    auto const attemptId=body["attemptId"].asString().unwrapOr("");
    auto const eventGeneration=++m_eventGeneration;
    m_eventRequest.spawn(http.post(matchEndpoint("/matches/"+id+"/events")),[this,generation,eventGeneration,session,id,eventId,eventType,attemptId](web::WebResponse response) {
        if(!current(generation,session) || m_view.id!=id || eventGeneration!=m_eventGeneration)return;
        m_eventBusy=false;
        if(eventType=="FORFEIT")geode::log::info("[SurvivalSurrender] HTTP event={} match={} status={}",eventId,id,response.code());
        if(response.code()>=200 && response.code()<300) {
            auto const accepted=response.json().unwrapOr(matjson::Value());
            if(eventType=="ATTEMPT_START")replay::Recorder::get().accepted(attemptId,true);
            if(eventType=="ATTEMPT_START" && accepted["myActiveAttemptId"].asString().unwrapOr("")==attemptId){
                m_spectatorAcceptedAttempt=attemptId;
            }
            observe(accepted);pumpSurrenderReturn();
            if(eventType=="FORFEIT")geode::log::info("[SurvivalSurrender] ACK event={} eliminated={} accepted={}",eventId,m_view.ownEliminated,m_surrenderAccepted);
            if(eventType=="FORFEIT" && eventId==m_surrenderEventId && !m_surrenderAccepted){
                m_retryAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);m_nextPoll={};return;
            }
            if(!m_events.empty() && m_events.front()["eventId"].asString().unwrapOr("")==eventId)m_events.pop_front();
            m_retryAt={};
        } else if(response.code()==409 || response.code()==400) {
            if(eventType=="ATTEMPT_START")replay::Recorder::get().accepted(attemptId,false);
            if(eventId==m_surrenderEventId)resetSurrender();
            if(eventType=="PRESENTATION_COMPLETE")m_presentationContext.clear();
            if(!m_events.empty() && m_events.front()["eventId"].asString().unwrapOr("")==eventId)m_events.pop_front();
            m_nextPoll={};m_view.error="Survival state changed. Synchronizing...";
        } else if((eventType=="FORFEIT" || surrenderPending()) && response.code()>=400 && response.code()<500 && response.code()!=408 && response.code()!=429) {
            if(!m_events.empty() && m_events.front()["eventId"].asString().unwrapOr("")==eventId)m_events.pop_front();
            auto const surrenderId=m_surrenderEventId;
            std::erase_if(m_events,[&](auto const& body){return body["eventId"].asString().unwrapOr("")==surrenderId;});
            resetSurrender();
            m_nextPoll={};m_view.error="Survival surrender was rejected. Synchronizing...";
        } else {m_retryAt=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);m_view.error="Survival server connection failed. Retrying...";}
    });
}
void SurvivalRuntime::discardObsoleteEvents() {
    auto obsolete=[&](matjson::Value const& body){
        auto const otherRound=body["roundKey"].asString().unwrapOr("")!=m_view.roundKey || body["mapGeneration"].asInt().unwrapOr(0)!=m_view.mapGeneration;
        if(otherRound && body["eventId"].asString().unwrapOr("")==m_surrenderEventId && !m_surrenderAccepted)resetSurrender();
        return otherRound || (body["type"].asString().unwrapOr("")=="PROGRESS" && m_view.state!="PLAYING");
    };
    // Cancel only an obsolete packet. A real Start/End for the current playing
    // round keeps its exact event ID, FIFO order, retry and acknowledgement.
    if(m_eventBusy && !m_events.empty() && obsolete(m_events.front())){
        ++m_eventGeneration;m_eventRequest.cancel();m_eventBusy=false;m_retryAt={};
    }
    auto const before=m_events.size();std::erase_if(m_events,obsolete);
    if(m_events.size()!=before)m_retryAt={};
}
void SurvivalRuntime::assetsReady(GJGameLevel* level) {
    if(m_view.ownEliminated || observing() || freePlaying() || m_selectedPanelContext!=optionsContext())return;
    auto const key=preparationContextText(preparationContext());
    if((m_view.state!="PREPARING" && !(m_view.online && !m_view.ownAssetsReady && (m_view.state=="ENTERING" || m_view.state=="COUNTDOWN" || m_view.state=="PLAYING"))) || m_readyContext==key || !RankedRoundPreparation::get().ready() || !validateNative(level))return;
    auto body=event("ASSETS_READY");if(!RankedRuntime::get().prepareSurvivalEnvironment(body))return;
    body["certificate"]=m_view.certificate;m_readyContext=key;enqueue(std::move(body));
}
void SurvivalRuntime::preparationFailed(std::string const& reason) {
    if(m_view.ownEliminated || observing() || freePlaying())return;
    if(m_view.state!="PREPARING" || m_failedContext==preparationContextText(preparationContext()) || !m_readyContext.empty())return;
    auto body=event("PREPARATION_FAILED");body["reason"]=reason;m_failedContext=preparationContextText(preparationContext());enqueue(std::move(body));
}
void SurvivalRuntime::audioPreparationFailed(RoundAudioFailure const& failure){
    if(!m_view.online || m_view.ownEliminated || observing() || freePlaying() || m_view.ownAssetsReady ||
        (m_view.state!="PREPARING" && m_view.state!="ENTERING") || failure.resource.id<=0)return;
    auto key=preparationContextText(preparationContext())+":"+std::to_string(static_cast<int>(failure.resource.kind))+":"+
        std::to_string(failure.resource.id)+":"+std::string(roundAudioCategoryName(failure.category));
    if(m_audioReported.contains(key) || m_audioReported.size()>=32)return;
    m_audioReported.insert(key);
    auto body=event("PREPARATION_FAILED");body["reason"]=std::string(roundAudioCategoryName(failure.category));
    matjson::Value report=matjson::Value::object();
    report["category"]=std::string(roundAudioCategoryName(failure.category));
    report["resourceKind"]=failure.resource.kind==RoundResourceKind::Sfx?"SFX":"SONG";
    report["resourceId"]=failure.resource.id;report["nativeCode"]=failure.nativeCode;
    body["audioFailure"]=std::move(report);enqueue(std::move(body));
}
void SurvivalRuntime::sceneEntered(int levelId) {
    if(m_view.ownEliminated || observing() || freePlaying())return;
    auto const key=preparationContextText(preparationContext());
    if(!gameplayLevel(levelId) || m_enteredContext==key)return;
    if(m_view.state=="ENTERING" || (m_view.online && !m_view.ownEntered && (m_view.state=="COUNTDOWN" || m_view.state=="PLAYING"))) {auto body=event("ENTERED");if(!RankedRuntime::get().prepareSurvivalEnvironment(body))return;
    enqueue(std::move(body));}
    m_enteredContext=key;Mod::get()->setSavedValue("survival-native-entered",optionsContext());
    // A killed process leaves a durable open attempt. Close its accepted record
    // before the fresh native scene can start another one, including in DM.
    if(!m_view.activeAttemptId.empty() && m_attempt.empty()) {
        auto body=event("ATTEMPT_END");body["attemptId"]=m_view.activeAttemptId;body["progress"]=m_view.activeAttemptProgress;enqueue(std::move(body));
    }
}
bool SurvivalRuntime::beginAttempt() {
    if(!canPlay() || !m_attempt.empty())return false;
    auto own=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& p){return p.id==m_view.ownId;});
    if(attemptLimitReached())return false;
    auto body=event("ATTEMPT_START");if(!RankedRuntime::get().prepareSurvivalEnvironment(body))return false;
    m_localAttempts=std::max(m_localAttempts,own==m_view.standings.end()?0:own->attemptsUsed)+1;
    m_spectatorAcceptedAttempt.clear();m_attempt=utils::random::generateUUID();m_attemptContext=preparationContextText(preparationContext());m_attemptBest=0;m_lastProgress=-1;m_nextProgress={};
    replay::Json metadata;metadata["formatVersion"]=replay::FormatVersion;metadata["nativeWorldEventVersion"]=1;metadata["gameVersion"]="2.2081";
    metadata["geodeVersion"]=Loader::get()->getVersion().toVString();metadata["modVersion"]=Mod::get()->getVersion().toVString();
    metadata["matchId"]=m_view.id;metadata["roundKey"]=m_view.roundKey;metadata["roundNumber"]=m_view.roundNumber;metadata["phase"]=m_view.phase;
    metadata["mapGeneration"]=m_view.mapGeneration;metadata["levelId"]=m_view.map.levelId;metadata["mapVersion"]=m_view.map.version;
    metadata["mapGeometrySha256"]=m_view.certificate["geometrySha256"];metadata["participantId"]=m_view.ownId;
    metadata["attemptId"]=m_attempt;metadata["attemptOrdinal"]=m_localAttempts;metadata["startedAt"]=m_clock.serverNowMillis(localNow());metadata["debug"]=m_view.debug;
    replay::Recorder::get().begin(std::move(metadata));
    body["attemptId"]=m_attempt;enqueue(std::move(body));return true;
}
void SurvivalRuntime::progress(double value) {
    if(!canPlay() || m_attempt.empty() || !std::isfinite(value) || value<0 || value>100)return;
    m_attemptBest=std::max(m_attemptBest,value);auto const now=std::chrono::steady_clock::now();
    if(now<m_nextProgress || value==m_lastProgress)return;
    m_nextProgress=now+std::chrono::milliseconds(250);m_lastProgress=value;
    if(m_events.size()>1 && m_events.back()["type"].asString().unwrapOr("")=="PROGRESS" && m_events.back()["attemptId"].asString().unwrapOr("")==m_attempt) {
        m_events.back()["progress"]=m_attemptBest;m_events.back()["currentProgress"]=value;return;
    }
    auto body=event("PROGRESS");body["attemptId"]=m_attempt;body["progress"]=m_attemptBest;body["currentProgress"]=value;enqueue(std::move(body));
}
void SurvivalRuntime::endAttempt(double value) {
    if(freePlaying() || observing())return;
    if(m_attempt.empty() || m_attemptContext!=preparationContextText(preparationContext()))return;
    if(std::isfinite(value) && value>=0 && value<=100)m_attemptBest=std::max(m_attemptBest,value);
    replay::Recorder::get().finish(value==100?"CLEAR":"EXIT",m_attemptBest);
    auto body=event("ATTEMPT_END");body["attemptId"]=std::exchange(m_attempt,{});body["progress"]=m_attemptBest;enqueue(std::move(body));
}
bool SurvivalRuntime::officialRoundCleared() const {
    if(m_view.ownEliminated||m_view.state!="PLAYING"||!m_attempt.empty()||!m_view.activeAttemptId.empty())return false;
    auto own=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& p){return p.id==m_view.ownId;});
    return own!=m_view.standings.end()&&own->cleared&&own->bestProgress==100&&!own->attemptActive;
}
bool SurvivalRuntime::normalRoundCleared() const {
    if(m_view.phase=="TIEBREAK")return false;
    auto own=std::find_if(m_view.standings.begin(),m_view.standings.end(),[&](auto const& p){return p.id==m_view.ownId;});
    return own!=m_view.standings.end() && own->cleared && own->bestProgress==100 && !own->attemptActive && m_view.activeAttemptId.empty();
}
bool SurvivalRuntime::canFreePlay() const {
    return active() && !freePlaying() && officialRoundCleared() && m_view.state=="PLAYING" && !m_view.ownEliminated &&
        m_session==RankedRuntime::get().spectatorLiveSessionToken() && !m_session.empty();
}
bool SurvivalRuntime::canEnterObserver() const {
    return observing() && watchLockSeconds()==0 && !freePlaying() && m_view.state=="PLAYING" && remainingMs()>0 &&
        m_prepared.valid() && m_prepared==preparationContext() && !m_session.empty() && m_session==RankedRuntime::get().spectatorLiveSessionToken();
}
bool SurvivalRuntime::armObserver(GJGameLevel* level,CCScene* origin){
    if(!canEnterObserver() || !validateNative(level) || !origin || origin!=CCDirector::sharedDirector()->getRunningScene())return false;
    m_freePlay.begin({m_view.id,m_view.roundKey,m_view.mapGeneration},m_view.completedRound["key"].asString().unwrapOr(""),true,m_view.map.levelId);
    m_freePlayOrigin.emplace(origin);m_freePlayNativeQuitFinished=false;m_freePlayQuitLevel=nullptr;m_freePlayQuitScene.reset();
    log::info("[SurvivalObserver] local-only launch match={} round={} level={}",m_view.id,m_view.roundKey,m_view.map.levelId);return true;
}
bool SurvivalRuntime::canReplayCleared() const {
    return canFreePlay() && remainingMs()>0 && m_prepared.valid() && m_prepared==preparationContext();
}
bool SurvivalRuntime::armClearedReplay(GJGameLevel* level,CCScene* origin){
    if(!canReplayCleared() || !validateNative(level) || !origin || origin!=CCDirector::sharedDirector()->getRunningScene())return false;
    m_freePlay.begin({m_view.id,m_view.roundKey,m_view.mapGeneration},m_view.completedRound["key"].asString().unwrapOr(""),false,m_view.map.levelId);
    m_freePlayOrigin.emplace(origin);m_freePlayNativeQuitFinished=false;m_freePlayQuitLevel=nullptr;m_freePlayQuitScene.reset();
    log::info("[SurvivalReplay] local-only cleared replay match={} round={} level={}",m_view.id,m_view.roundKey,m_view.map.levelId);return true;
}
void SurvivalRuntime::leaveObserver(){
    if(!observing())return;
    chooseOutcome(SurvivalOutcomeChoice::Undecided);
    if(m_freePlay.active && m_freePlay.observer)m_freePlay.returnRequested=true;
    pumpFreePlayReturn();
}
bool SurvivalRuntime::beginFreePlay() {
    auto* director=CCDirector::sharedDirector();auto* scene=director->getRunningScene();
    if(!canFreePlay() || !scene || !scene->getChildByID("survival-game-coordinator"_spr) || director->getNextScene())return false;
    m_freePlay.begin({m_view.id,m_view.roundKey,m_view.mapGeneration},m_view.completedRound["key"].asString().unwrapOr(""));
    m_freePlayOrigin.emplace(scene);m_freePlayNativeQuitFinished=false;m_freePlayQuitLevel=nullptr;m_freePlayQuitScene.reset();
    queueInMainThread([this,weak=WeakRef(scene)]{
        auto origin=weak.lock();auto* director=CCDirector::sharedDirector();
        if(!origin || director->getRunningScene()!=origin.data() || director->getNextScene() || freePlayReturnPending()){
            if(!freePlayReturnPending()){m_freePlay.finish();m_freePlayOrigin.reset();}return;
        }
        RankedAudioManager::get().restoreMenuMusic();
        director->pushScene(MenuLayer::scene(false));
    });return true;
}
void SurvivalRuntime::requestDirectFreePlayReturn(PlayLayer* owner) {
    // Only the directly launched replay/observer map belongs to this return.
    // Ordinary menu Free Play remains open until authoritative round change.
    if(!m_freePlay.active || m_freePlay.directLevelId<=0 || !owner || owner!=PlayLayer::get() ||
       !owner->m_level || static_cast<int>(owner->m_level->m_levelID)!=m_freePlay.directLevelId ||
       owner->getParent()!=CCDirector::sharedDirector()->getRunningScene())return;
    m_freePlay.returnRequested=true;
}
bool SurvivalRuntime::freePlayReturnPending() const {
    return m_freePlay.needsReturn({m_view.id,m_view.roundKey,m_view.mapGeneration},m_view.state,
        m_view.completedRound["key"].asString().unwrapOr(""),m_view.ownEliminated);
}
bool SurvivalRuntime::beginFreePlayNativeQuit(GJGameLevel* level) {
    if(!freePlayReturnPending() || !level)return false;
    m_freePlayQuitLevel=level;m_freePlayQuitScene.emplace(CCDirector::sharedDirector()->getRunningScene());return true;
}
bool SurvivalRuntime::interceptFreePlayNativeQuit(GJGameLevel* level) const {
    return freePlayReturnPending() && m_freePlayQuitLevel && m_freePlayQuitLevel.data()==level;
}
void SurvivalRuntime::finishFreePlayNativeQuit() {m_freePlayNativeQuitFinished=true;pumpFreePlayReturn();}
void SurvivalRuntime::pumpFreePlayReturn() {
    if(!freePlayReturnPending() || m_freePlayReturnQueued)return;
    auto* director=CCDirector::sharedDirector();auto* scene=director->getRunningScene();
    if(!scene || director->getNextScene() || typeinfo_cast<CCTransitionScene*>(scene))return;
    m_freePlayReturnQueued=true;
    queueInMainThread([this,weak=WeakRef(scene)]{
        m_freePlayReturnQueued=false;auto scene=weak.lock();auto* director=CCDirector::sharedDirector();
        if(!scene || director->getRunningScene()!=scene.data() || director->getNextScene() || !freePlayReturnPending())return;
        auto quitScene=m_freePlayQuitScene?m_freePlayQuitScene->lock():Ref<CCScene>();
        auto* play=PlayLayer::get();
        if(!(m_freePlayNativeQuitFinished && quitScene.data()==scene.data()) && play && play->getParent()==scene.data()){
            requestSurvivalGameplayReturn(play);return;
        }
        auto origin=m_freePlayOrigin?m_freePlayOrigin->lock():Ref<CCScene>();
        m_freePlay.finish();m_freePlayQuitLevel=nullptr;m_freePlayQuitScene.reset();m_freePlayNativeQuitFinished=false;
        m_freePlayOrigin.reset();
        if(origin && director->levelForSceneInStack(origin.data())>=0){
            if(origin.data()!=scene.data())director->popToSceneInStack(origin.data());
        }else showSurvivalGame();
    });
}
void SurvivalRuntime::resetSurrender() {
    m_surrenderEventId.clear();m_surrenderMatchId.clear();m_surrenderAccepted=false;
    m_surrenderOwner.reset();m_surrenderScene.reset();
}
bool SurvivalRuntime::canSurrender() const {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    if(forceClearPending())return false;
#endif
    return active() && !m_session.empty() && m_session==RankedRuntime::get().spectatorLiveSessionToken() &&
        !surrenderPending() && !normalRoundCleared() && !freePlaying() && !m_view.ownEliminated &&
        (m_view.state=="MAP_SELECTION" || m_view.state=="PREPARING" || m_view.state=="ENTERING" || m_view.state=="COUNTDOWN" || m_view.state=="PLAYING");
}
void SurvivalRuntime::surrender() {
    if(!active() || m_session.empty() || !m_surrenderEventId.empty() || m_view.ownEliminated ||
        m_view.state=="FINAL_RESULT" || m_view.state=="CANCELLED" || m_events.size()>254)return;
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
    if(forceClearPending())return;
#endif
    auto body=event("FORFEIT");m_surrenderEventId=body["eventId"].asString().unwrapOr("");m_surrenderMatchId=m_view.id;
    auto* play=PlayLayer::get();auto* scene=CCDirector::sharedDirector()->getRunningScene();
    if(isOfficialSurvivalPlayLayer(play) && scene && play->getParent()==scene){m_surrenderOwner.emplace(play);m_surrenderScene.emplace(scene);}
    geode::log::info("[SurvivalSurrender] submit event={} match={} round={} generation={} queued={}",m_surrenderEventId,m_view.id,m_view.roundKey,m_view.mapGeneration,m_events.size());
    endAttempt(m_view.activeAttemptProgress);enqueue(std::move(body));
}
void SurvivalRuntime::pumpSurrenderReturn() {
    if(m_surrenderEventId.empty() || m_view.id!=m_surrenderMatchId || !m_view.ownEliminated)return;
    // A successful event response (or recovery poll) must contain the server's
    // elimination before any scene exit. Online matches may still be PLAYING.
    if(!m_surrenderAccepted){m_surrenderAccepted=true;chooseOutcome(SurvivalOutcomeChoice::PersonalResult);}
    auto owner=m_surrenderOwner?m_surrenderOwner->lock():Ref<PlayLayer>();
    auto scene=m_surrenderScene?m_surrenderScene->lock():Ref<CCScene>();
    auto* director=CCDirector::sharedDirector();
    if(!owner || !scene || owner.data()!=PlayLayer::get() ||
        owner->getParent()!=scene.data() || director->getRunningScene()!=scene.data() || director->getNextScene())return;
    requestSurvivalGameplayReturn(owner.data());
}
void SurvivalRuntime::presentationComplete() {
    if(m_view.online || !active() || m_view.presentationGate.empty() || m_view.result.isObject())return;
    auto const key=m_view.id+":"+m_view.roundKey+":"+std::to_string(m_view.mapGeneration)+":"+m_view.presentationGate;
    if(m_presentationContext==key)return;
    m_presentationContext=key;enqueue(event("PRESENTATION_COMPLETE"));
}
void SurvivalRuntime::acknowledge() {
    if((!m_view.result.isObject() && m_view.state!="CANCELLED") || m_creating || std::chrono::steady_clock::now()<m_ackRetryAt)return;
    m_creating=true;auto const generation=m_generation;auto const session=m_session;auto const id=m_view.id;
    m_controlRequest.spawn(request(session).post(matchEndpoint("/matches/"+id+"/acknowledge")),[this,generation,session,id](web::WebResponse response) {
        if(!current(generation,session) || m_view.id!=id)return;
        m_creating=false;
        if(response.code()<200 || response.code()>=300){
            m_view.error="Survival server connection failed. Retrying...";
            auto const wait=response.json().unwrapOr(matjson::Value())["retryAfterSeconds"].asInt().unwrapOr(2);
            m_ackRetryAt=std::chrono::steady_clock::now()+std::chrono::seconds(std::clamp<std::int64_t>(wait,2,60));return;
        }
        ++m_generation;m_pollRequest.cancel();m_eventRequest.cancel();m_pollBusy=m_eventBusy=false;m_view={};m_queue={};m_events.clear();m_prepared={};
        resetSurrender();m_clock={};m_lastServerNow=0;
        Mod::get()->setSavedValue("survival-debug-match",std::string{});RankedRuntime::get().fetchSurvivalProfile();RankedRuntime::get().fetchAchievements();
    });
}
std::optional<ScoreAnimationInput> SurvivalRuntime::scoreAnimation() const {
    auto const& result=m_view.result;if(!result.isObject())return std::nullopt;
    ScoreAnimationInput input;input.matchId=m_view.id;input.before=static_cast<int>(result["profileBefore"]["lp"].asInt().unwrapOr(0));
    input.after=static_cast<int>(result["profileAfter"]["lp"].asInt().unwrapOr(0));input.beforeTier=result["profileBefore"]["tier"].asString().unwrapOr("");
    input.afterTier=result["profileAfter"]["tier"].asString().unwrapOr("");input.promoted=result["promoted"].asBool().unwrapOr(false);input.demoted=result["demoted"].asBool().unwrapOr(false);
    input.survivalRank=static_cast<int>(result["rank"].asInt().unwrapOr(16));
    input.won=input.after>input.before;input.demotionWarning=result["profileAfter"]["demotionWarning"].asBool().unwrapOr(false);
    for(auto const& b:result["bands"].asArray().unwrapOr(std::vector<matjson::Value>{}))input.bands.push_back({b["tier"].asString().unwrapOr(""),static_cast<int>(b["minimum"].asInt().unwrapOr(0))});
    if(!validScoreBands(input.bands))return std::nullopt;return input;
}
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
bool SurvivalRuntime::canForceClear() const {
    return !forceClearPending() && !surrenderPending() && m_view.debug && !m_view.online && !m_view.ownEliminated && !normalRoundCleared() &&
        m_view.phase!="TIEBREAK" && m_view.ownAssetsReady && m_view.ownEntered && canPlay();
}
void SurvivalRuntime::forceClear() {
    if(!canForceClear() || forceClearPending())return;
    m_forceClearBody.emplace(event("FORCE_CLEAR"));(*m_forceClearBody)["requestId"]=utils::random::generateUUID();
    m_forceClearBody->erase("type");m_forceClearBody->erase("eventId");m_forceClearRetryAt={};geode::log::info("[SurvivalForceClear] submit request={} match={} round={} generation={}",(*m_forceClearBody)["requestId"].asString().unwrapOr(""),m_view.id,m_view.roundKey,m_view.mapGeneration);pumpForceClear();
}
void SurvivalRuntime::pumpForceClear() {
    if(!forceClearPending())return;
    auto const ending=(*m_forceClearBody)["type"].asString().unwrapOr("")=="DEBUG_END_ROUND";
    if((*m_forceClearBody)["roundKey"].asString().unwrapOr("")!=m_view.roundKey ||
        (*m_forceClearBody)["mapGeneration"].asInt().unwrapOr(0)!=m_view.mapGeneration || !active() ||
        m_view.state!="PLAYING" || m_view.ownEliminated){
        m_forceClearRequest.cancel();m_forceClearBusy=false;m_forceClearBody.reset();m_view.error="Survival state changed. Synchronizing...";return;
    }
    // Poll recovers a committed request whose HTTP response was lost.
    if(!ending && normalRoundCleared()){
        // Only an approved synthetic clear retires the native capture. A
        // rejected Force Clear must not disable a still-official Attempt.
        replay::Recorder::get().finishFromLast("DEBUG_FORCE_CLEAR");
        m_forceClearRequest.cancel();m_forceClearBusy=false;m_forceClearBody.reset();m_attempt.clear();return;
    }
    if(m_forceClearBusy || m_eventBusy || !m_events.empty() || std::chrono::steady_clock::now()<m_forceClearRetryAt)return;
    m_forceClearBusy=true;auto http=request(m_session);auto payload=*m_forceClearBody;payload.erase("type");http.bodyJSON(payload);
    auto const generation=m_generation;auto const session=m_session,id=m_view.id,roundKey=m_view.roundKey;auto const mapGeneration=m_view.mapGeneration;
    auto const requestId=(*m_forceClearBody)["requestId"].asString().unwrapOr("");
    m_forceClearRequest.spawn(http.post(debugEndpoint("/matches/"+id+(ending?"/end-round":"/force-clear"))),[this,generation,session,id,roundKey,mapGeneration,requestId,ending](web::WebResponse response){
        if(!current(generation,session) || id!=m_view.id || roundKey!=m_view.roundKey || mapGeneration!=m_view.mapGeneration || !m_forceClearBody || (*m_forceClearBody)["requestId"].asString().unwrapOr("")!=requestId)return;
        m_forceClearBusy=false;
        geode::log::info("[SurvivalForceClear] HTTP request={} match={} round={} status={}",requestId,id,roundKey,response.code());
        if(response.code()>=200 && response.code()<300){
            observe(response.json().unwrapOr(matjson::Value()));m_nextPoll={};
            if((ending && m_view.state!="PLAYING") || (!ending && normalRoundCleared())){if(!ending)replay::Recorder::get().finishFromLast("DEBUG_FORCE_CLEAR");m_forceClearBody.reset();m_attempt.clear();}
            else m_forceClearRetryAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);
        }else if(response.code()==0 || response.code()==408 || response.code()==429 || response.code()>=500){
            m_forceClearRetryAt=std::chrono::steady_clock::now()+std::chrono::seconds(1);m_nextPoll={};
            m_view.error="Survival server connection failed. Retrying...";
        }else {m_forceClearBody.reset();m_nextPoll={};m_view.error="survival-force-clear-rejected";}
    });
}
bool SurvivalRuntime::canEndRound() const {
    return !freePlaying() && !forceClearPending() && !surrenderPending() && m_view.debug && !m_view.online && !m_view.ownEliminated &&
        m_view.ownAssetsReady && m_view.ownEntered && m_view.state=="PLAYING" && remainingMs()>0 &&
        !m_session.empty() && m_session==RankedRuntime::get().spectatorLiveSessionToken();
}
void SurvivalRuntime::endRound(){
    if(!canEndRound())return;
    m_forceClearBody.emplace(event("DEBUG_END_ROUND"));(*m_forceClearBody)["requestId"]=utils::random::generateUUID();m_forceClearBody->erase("eventId");
    endAttempt(m_view.activeAttemptProgress);
    m_forceClearRetryAt={};log::info("[SurvivalEndRound] submit match={} round={} request={}",m_view.id,m_view.roundKey,(*m_forceClearBody)["requestId"].asString().unwrapOr(""));pumpForceClear();
}
void SurvivalRuntime::start(std::string password,std::string group,int forceRank,int botCount,bool forceDeathMatch) {
    tick();if(active()){showSurvivalGame();return;}if(queueing()){showSurvivalQueue();return;}if(m_creating || m_session.empty())return;
    matjson::Value body;if(!RankedRuntime::get().prepareSurvivalDebugAdmission(body))return;
    body["password"]=std::move(password);body["requestId"]=utils::random::generateUUID();body["averageGroup"]=std::move(group);body["forceDeathMatch"]=forceDeathMatch;if(botCount>=0)body["botCount"]=botCount;if(forceRank>0)body["forceRank"]=forceRank;
    auto http=request(m_session);http.timeout(std::chrono::seconds(70));http.bodyJSON(body);m_creating=true;
    auto const generation=m_generation;auto const session=m_session;
    m_controlRequest.spawn(http.post(debugEndpoint("/matches")),[this,generation,session](web::WebResponse response) {
        if(!current(generation,session))return;
        m_creating=false;
        auto const root=response.json().unwrapOr(matjson::Value());
        if(response.code()<200 || response.code()>=300){
            if(RankedRuntime::get().handleSurvivalAdmissionError(response))return;
            m_view.error=root["message"].asString().unwrapOr("Survival debug match could not start.");
            if(response.code()==0 || response.code()>=500 || response.code()==409)recover();
            return;
        }
        observe(root);queueInMainThread([] {if(SurvivalRuntime::get().active())showSurvivalGame();});
    });
}
void SurvivalRuntime::forceRank(std::string password,int rank) {
    if(!active() || !m_view.debug || m_view.online || m_creating || rank<1 || rank>m_view.playerCount)return;
    matjson::Value body;body["password"]=std::move(password);body["rank"]=rank;
    auto http=request(m_session);http.bodyJSON(body);m_creating=true;auto const generation=m_generation;auto const session=m_session;auto const id=m_view.id;
    m_controlRequest.spawn(http.post(debugEndpoint("/matches/"+id+"/force-rank")),[this,generation,session,id](web::WebResponse response) {
        if(!current(generation,session) || m_view.id!=id)return;
        m_creating=false;
        if(response.code()>=200 && response.code()<300)observe(response.json().unwrapOr(matjson::Value()));
        else m_view.error="Survival debug rank could not be forced.";
    });
}
void SurvivalRuntime::fillBots(std::string password,int botCount,bool forceDeathMatch) {
    if(m_creating || m_queue.status!="QUEUED" || active())return;
    matjson::Value body;if(!RankedRuntime::get().prepareSurvivalAdmission(body))return;
    body["password"]=std::move(password);body["requestId"]=utils::random::generateUUID();if(botCount>=0)body["botCount"]=botCount;body["forceDeathMatch"]=forceDeathMatch;queueAction("/fill-bots",std::move(body));
}
#endif
}
