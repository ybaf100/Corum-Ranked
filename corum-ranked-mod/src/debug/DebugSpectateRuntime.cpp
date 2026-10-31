#include "DebugSpectateRuntime.hpp"
#include "../spectator/SpectatorPoseCodec.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../RankedRuntime.hpp"
#include "../RankedLevelPreparation.hpp"
#include "../SpectatorLiveRuntime.hpp"
#include <Geode/binding/LevelInfoLayer.hpp>

using namespace geode::prelude;
namespace {
constexpr std::string_view kServer = "https://api.52-74-37-130.sslip.io";
std::string endpoint(std::string const& path) { return std::string(kServer) + "/api/ranked/debug/spectate/" + path; }
bool success(web::WebResponse const& response) { return response.code() >= 200 && response.code() < 300; }
web::WebRequest request(std::string const& token = {}) {
    web::WebRequest result;
    result.header("x-corum-debug-spectate", "1");
    if (!token.empty()) result.header("x-debug-spectate-token", token);
    result.followRedirects(false);
    result.timeout(std::chrono::seconds(3));
    return result;
}
class DebugSpectatePump final : public CCNode {
public:
    void update(float) override { corum::ranked::debug::DebugSpectateRuntime::get().tick(); }
};
void installPump() {
    static auto* pump = new DebugSpectatePump;
    static bool installed = false;
    if (installed) return;
    installed = true;
    // Scene-independent callbacks must drain terminal frames after vanilla
    // death/Clear stops PlayLayer updates. No frame file or local replay exists.
    CCDirector::sharedDirector()->getScheduler()->scheduleUpdateForTarget(pump, 0, false);
}
}
namespace corum::ranked::debug {
DebugSpectateRuntime& DebugSpectateRuntime::get() { static auto* value = new DebugSpectateRuntime; return *value; }
void DebugSpectateRuntime::refreshLiveSetting() {
    if (m_settingBusy) return;
    auto http = request();
    http.header("Authorization", "Bearer " + RankedRuntime::get().spectatorLiveSessionToken());
    m_settingBusy = true;
    m_settingRequest.spawn(http.get(endpoint("live-setting")), [this](web::WebResponse response) {
        m_settingBusy = false;
        auto const enabled = response.json().unwrapOr(matjson::Value())["liveEnabled"].asBool();
        if (!success(response) || enabled.isErr()) {
            m_liveEnabled.reset(); m_status = "LIVE SETTING UNAVAILABLE / SERVER DEBUG DISABLED"; return;
        }
        m_liveEnabled = enabled.unwrap();
    });
}
void DebugSpectateRuntime::toggleLiveSetting(std::string password) {
    if (m_settingBusy || !m_liveEnabled) return;
    matjson::Value body; body["password"] = password; body["liveEnabled"] = !*m_liveEnabled;
    body["levelId"] = kDebugSpectateLevelId; body["formatVersion"] = kDebugSpectateFormatVersion;
    std::fill(password.begin(), password.end(), '\0');
    auto http = request(); http.header("Authorization", "Bearer " + RankedRuntime::get().spectatorLiveSessionToken()); http.bodyJSON(body);
    m_settingBusy = true;
    m_settingRequest.spawn(http.post(endpoint("live-setting")), [this](web::WebResponse response) {
        m_settingBusy = false;
        auto const enabled = response.json().unwrapOr(matjson::Value())["liveEnabled"].asBool();
        if (!success(response) || enabled.isErr()) {
            m_liveEnabled.reset(); m_status = "LIVE SETTING CHANGE FAILED - CHECK SERVER PASSWORD"; return;
        }
        m_liveEnabled = enabled.unwrap();
        m_status = *m_liveEnabled ? "LIVE SPECTATOR ENABLED FOR ALL MATCHES" : "LIVE SPECTATOR DISABLED - PERCENTAGE WAIT SCREEN";
    });
}
void DebugSpectateRuntime::start(bool playback, std::string password,float startPercent,int levelId) {
    if(!std::isfinite(startPercent)||startPercent<0||startPercent>100){m_status="ENTER A START PERCENT FROM 0 TO 100";return;}
    auto& ranked = RankedRuntime::get();
    if (m_mode != DebugSpectateMode::None || m_busy || ranked.view().stage != RuntimeStage::Ready ||
        ranked.hasLocalGameplayAuthority() || ranked.spectatorLiveSessionToken().empty()) {
        m_status = "RETURN TO RANKED LOBBY / WAIT FOR CURRENT DEBUG SESSION"; return;
    }
    if(!playback && levelId<=0){m_status="ENTER A VALID LEVEL ID";return;}
    m_levelId=playback?0:levelId;
    m_startPercent=playback?startPercent:0;
    installPump();
    m_mode = playback ? DebugSpectateMode::PreparingPlayback : DebugSpectateMode::PreparingRecord;
    m_status = playback ? "REQUESTING SERVER PLAYBACK..." : "CREATING SERVER RECORDING...";
    m_launched = false; m_playbackComplete = false; m_errors = 0; m_capture = {};m_evidenceLogs=0;
    m_previousCapturedPose.reset();m_nativeClockOrigin.reset();
    m_id.clear(); m_token.clear(); m_contextId.clear();
    // Admission belongs to a NEW scope. A deadline/backoff left by an expired
    // or failed scope must not cancel this request before its HTTP ACK arrives.
    m_expireAt = {}; m_nextUploadAt = {}; m_recordStartedAt = {};
    m_prepareStartedAt = std::chrono::steady_clock::now();
    matjson::Value body;
    body["password"] = password; body["levelId"] = playback?kDebugSpectateLevelId:m_levelId; body["formatVersion"] = kDebugSpectateFormatVersion;
    if(playback)body["startPercent"]=static_cast<double>(m_startPercent);
    std::fill(password.begin(), password.end(), '\0');
    auto http = request(); http.header("Authorization", "Bearer " + ranked.spectatorLiveSessionToken()); http.bodyJSON(body);
    auto const generation = ++m_generation; m_busy = true;
    m_request.spawn(http.post(endpoint(playback ? "playbacks" : "recordings")), [this, generation, playback](web::WebResponse response) {
        if (generation != m_generation) return;
        m_busy = false;
        auto const root = response.json().unwrapOr(matjson::Value());
        if (!success(response)) {
            fail(root["message"].asString().unwrapOr("DEBUG SPECTATE SERVER UNAVAILABLE / DISABLED")); return;
        }
        auto admitted=root["levelId"].asInt().unwrapOr(0);
        if (admitted<=0 || admitted>2147483647 || (!playback && admitted!=m_levelId) ||
            root["formatVersion"].asInt().unwrapOr(0) != kDebugSpectateFormatVersion) { fail("INVALID DEBUG SERVER CONTEXT"); return; }
        m_levelId=static_cast<int>(admitted);
        m_id = root[playback ? "playbackId" : "recordingId"].asString().unwrapOr("");
        m_token = root["token"].asString().unwrapOr("");
        m_contextId = playback ? root["contextId"].asString().unwrapOr("") : "debug-record:" + m_id;
        if (m_id.empty() || m_token.empty() || m_contextId.empty()) { fail("MISSING DEBUG SERVER SCOPE"); return; }
        m_playbackStartElapsedMs=playback?root["playbackStartElapsedMs"].asDouble().unwrapOr(0):0;
        if(!std::isfinite(m_playbackStartElapsedMs)||m_playbackStartElapsedMs<0||m_playbackStartElapsedMs>900000){fail("INVALID SOURCE PLAYBACK TIME");return;}
        log::info("[DebugSpectate] requestedPercent={} sourcePlaybackStartElapsedMs={} (native audio timeline comes from the recorded sample)",m_startPercent,m_playbackStartElapsedMs);
        m_expireAt = m_prepareStartedAt + std::chrono::minutes(15);
        m_status = playback && m_startPercent>0?fmt::format("PREPARING RECORDED MAP - JOIN AT {:.2f}%",m_startPercent):"PREPARING MAP - VERIFIED GD METADATA / VANILLA DOWNLOAD";
        log::info("[DebugSpectate] {} created id={}", playback ? "playback" : "recording", m_id);
    });
}
bool DebugSpectateRuntime::preparedLevel(int levelId) const {
    return RankedRuntime::get().view().stage == RuntimeStage::Ready && levelId == m_levelId && !m_token.empty() &&
        (m_mode == DebugSpectateMode::PreparingRecord || m_mode == DebugSpectateMode::PreparingPlayback);
}
bool DebugSpectateRuntime::recordLevel(int levelId) const {
    return levelId == m_levelId && !m_token.empty() &&
        (m_mode == DebugSpectateMode::Recording || m_mode == DebugSpectateMode::FinalizingRecord);
}
bool DebugSpectateRuntime::playbackLevel(int levelId) const {
    return RankedRuntime::get().view().stage == RuntimeStage::Ready && levelId == m_levelId &&
        !m_token.empty() && m_mode == DebugSpectateMode::Playback;
}
LevelPreparationContext DebugSpectateRuntime::preparationContext() const {
    if (m_token.empty() || m_contextId.empty() || m_mode == DebugSpectateMode::None) return {};
    return {m_contextId, m_levelId, 1, 0, false, 0, 0};
}
void DebugSpectateRuntime::tick() {
    auto const now = std::chrono::steady_clock::now();
    if (m_mode == DebugSpectateMode::None) return;
    if (m_expireAt.time_since_epoch().count() && now >= m_expireAt) {
        fail("DEBUG SPECTATE SESSION EXPIRED"); return;
    }
    if (m_mode == DebugSpectateMode::Recording || m_mode == DebugSpectateMode::FinalizingRecord) { pumpRecording(); return; }
    if (RankedRuntime::get().view().stage != RuntimeStage::Ready) { cancelPreparation("Ranked lobby authority changed"); return; }
    if (m_mode == DebugSpectateMode::Playback) return;
    if (now - m_prepareStartedAt > std::chrono::minutes(2)) { cancelPreparation("map preparation timeout"); m_status = "MAP PREPARATION TIMEOUT"; return; }
    if (!preparedLevel(m_levelId) || m_launched || m_busy) return;
    auto& preparation = RankedLevelPreparation::get(); preparation.poll();
    auto* level = preparation.level();
    if (!level || !preparation.authorizes(level, preparationContext())) return;
    log::info("[DebugSpectate] preparation metadata verified context={}", m_contextId);
    if (preparation.adoptVerifiedSavedLevel(preparationContext())) {
        level = preparation.level();
        log::info("[DebugSpectate] using downloaded saved map geometry");
        tracePreparedLevel("debug-saved-launch", level);
    }
    // Ownership passes to LevelInfo BEFORE native construction/push. A popup
    // close during scene teardown is not a user cancellation of that new owner.
    m_launched = true;
    auto* scene = LevelInfoLayer::scene(level, false);
    if (!scene) { fail("CANNOT OPEN VERIFIED MAP LEVELINFO"); return; }
    CCDirector::sharedDirector()->pushScene(CCTransitionFade::create(0.3f, scene));
}
void DebugSpectateRuntime::sceneEntered(int levelId) {
    if (!preparedLevel(levelId)) return;
    if (m_mode == DebugSpectateMode::PreparingRecord) {
        m_mode = DebugSpectateMode::Recording; m_recordStartedAt = std::chrono::steady_clock::now();
        m_status = "RECORD SPECTATE - SERVER RECORDING";
        log::info("[DebugSpectate] record started recordingId={}", m_id);
    } else {
        m_mode = DebugSpectateMode::Playback; m_status = "LIVE SPECTATOR - WAITING FOR SERVER";
        SpectatorLiveRuntime::get().startDebugViewer(m_contextId, m_token,
            "/api/ranked/debug/spectate/playbacks/" + m_id + "/spectator-live",m_startPercent>0);
    }
}
bool DebugSpectateRuntime::recordPose(SpectatorGhostPose const& pose, bool terminal) {
    if (m_mode != DebugSpectateMode::Recording || m_capture.ended()) return false;
    auto const now = std::chrono::steady_clock::now();
    if (m_capture.nextSequence() == 0 && finiteSpectatorPose(pose)) m_recordStartedAt = now;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_recordStartedAt).count();
    if(pose.nativeElapsedMs&&std::isfinite(*pose.nativeElapsedMs)&&*pose.nativeElapsedMs>=0){
        if(!m_nativeClockOrigin)m_nativeClockOrigin=*pose.nativeElapsedMs;
        if(*pose.nativeElapsedMs<*m_nativeClockOrigin){fail("RECORD_NATIVE_CLOCK_REVERSED");return false;}
        elapsed=static_cast<std::int64_t>(*pose.nativeElapsedMs-*m_nativeClockOrigin);
    }
    auto const sequence = m_capture.nextSequence();
    auto const accepted = m_capture.capture(elapsed, pose, terminal);
    if (accepted) {
        if(m_levelId==143308401&&elapsed>=70000&&elapsed<=73000&&m_evidenceLogs++<128){auto wire=encodeSpectatorPose(pose);
            log::info("[SpectatorEvidence] stage=DEBUG_CAPTURED attempt={} sequence={} elapsedMs={} p1={} p2={}",m_id,sequence,elapsed,wire["discontinuity"].dump(matjson::NO_INDENTATION),wire["player2"]["discontinuity"].dump(matjson::NO_INDENTATION));}

        if (sequence >= 18 && sequence <= 22 && m_previousCapturedPose) {
            auto const& previous = *m_previousCapturedPose;
            log::info("[DebugSpectate] capture forensic sequence={} elapsedMs={} x={} y={} deltaX={} deltaY={} distance={} previousMode={} mode={} mini={} upsideDown={} flipped={} dual={} cameraX={} cameraY={} discontinuity={}",
                sequence, elapsed, pose.x, pose.y, pose.x - previous.x, pose.y - previous.y,
                std::hypot(pose.x - previous.x, pose.y - previous.y), spectatorModeName(previous.mode),
                spectatorModeName(pose.mode), pose.mini, pose.upsideDown, pose.flipped, pose.dual,
                pose.camera ? pose.camera->x : 0, pose.camera ? pose.camera->y : 0,
                pose.discontinuity ? spectatorDiscontinuityName(pose.discontinuity->type) : "NONE");
        }
        m_previousCapturedPose = pose;
    }
    if (m_capture.ended()) m_mode = DebugSpectateMode::FinalizingRecord;
    pumpRecording();
    return accepted;
}
void DebugSpectateRuntime::cancelRecording() {
    if (m_mode != DebugSpectateMode::Recording) return;
    m_capture.cancel(); m_mode = DebugSpectateMode::FinalizingRecord; pumpRecording();
}
void DebugSpectateRuntime::pumpRecording() {
    if (m_busy || std::chrono::steady_clock::now() < m_nextUploadAt || m_token.empty()) return;
    auto batch = m_capture.batch();
    if (batch.empty() && !m_capture.ended()) return;
    auto http = request(m_token); matjson::Value body;
    if (!batch.empty()) {
        body["formatVersion"] = kDebugSpectateFormatVersion;
        auto frames = matjson::Value::array();
        std::size_t bytes=256, sent=0;
        for (auto const& f : batch) {
            matjson::Value frame; frame["sequence"] = f.sequence; frame["elapsedMs"] = f.elapsedMs;
            frame["keyframe"] = encodeSpectatorPose(f.pose);
            if(f.pose.cosmetics && validSpectatorCosmetics(*f.pose.cosmetics)){
                frame["keyframe"]["cosmetics"]=encodeSpectatorCosmetics(*f.pose.cosmetics);
            }
            if(m_levelId==143308401&&f.elapsedMs>=70000&&f.elapsedMs<=73000&&m_evidenceLogs++<128)
                log::info("[SpectatorEvidence] stage=DEBUG_ENCODED_UPLOAD attempt={} sequence={} elapsedMs={} p1={} p2={}",m_id,f.sequence,f.elapsedMs,frame["keyframe"]["discontinuity"].dump(matjson::NO_INDENTATION),frame["keyframe"]["player2"]["discontinuity"].dump(matjson::NO_INDENTATION));
            auto size=frame.dump().size();
            if(bytes+size>48*1024){if(sent==0){fail("RECORD_FRAME_BYTE_LIMIT");return;}break;}
            bytes+=size;++sent;frames.push(frame);
        }
        batch.resize(sent); // ACK only the bounded prefix actually sent.
        body["frames"] = frames;
    } else body["completed"] = m_capture.completed();
    http.bodyJSON(body); auto const generation = m_generation; auto const id = m_id;
    m_busy = true;
    m_request.spawn(http.post(endpoint("recordings/" + id + (batch.empty() ? "/finalize" : "/frames"))),
        [this, generation, id, batch](web::WebResponse response) {
        if (generation != m_generation || id != m_id) return;
        m_busy = false;
        if (!success(response)) {
            if (++m_errors >= 4 || (response.code() >= 400 && response.code() < 500)) { fail("SERVER RECORDING FAILED - INCOMPLETE, NOT TEST ELIGIBLE"); return; }
            m_nextUploadAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(150); return;
        }
        m_errors = 0; m_nextUploadAt = {};
        if (!batch.empty()) {
            auto const ack = response.json().unwrapOr(matjson::Value())["lastSequence"].asInt().unwrapOr(-1);
            if (ack < batch.back().sequence) { fail("INVALID RECORDING ACK"); return; }
            // Drain on the next scene-independent pump tick, never replace this
            // TaskHolder from inside its own completion callback.
            m_capture.acknowledge(batch.back().sequence); return;
        }
        m_status = m_capture.completed() ? "RECORDING PERSISTED - READY FOR TEST SPECTATE" : "RECORDING CANCELLED / INCOMPLETE";
        log::info("[DebugSpectate] record finalized recordingId={} completed={}", id, m_capture.completed());
        m_mode = DebugSpectateMode::None; m_token.clear(); m_contextId.clear();
    });
}
void DebugSpectateRuntime::viewerResponse(bool complete, bool expired) {
    if (m_mode != DebugSpectateMode::Playback) return;
    if (expired) { fail("DEBUG PLAYBACK ENDED / EXPIRED"); return; }
    if (complete && !m_playbackComplete) {
        m_playbackComplete = true; m_status = "SERVER PLAYBACK COMPLETE - TERMINAL POSE";
        log::info("[DebugSpectate] playback completed playbackId={}", m_id);
    }
}
void DebugSpectateRuntime::closePlayback() {
    if (m_id.empty() || m_token.empty()) return;
    auto http = request(m_token); http.bodyJSON(matjson::Value());
    m_cleanupRequest.spawn(http.post(endpoint("playbacks/" + m_id + "/close")), [](web::WebResponse) {});
    SpectatorLiveRuntime::get().stopDebugViewer();
    log::info("[DebugSpectate] playback cleaned up playbackId={}", m_id);
}
void DebugSpectateRuntime::exitScene() {
    if (m_mode == DebugSpectateMode::Playback) {
        closePlayback(); m_mode = DebugSpectateMode::None; m_token.clear(); m_contextId.clear(); ++m_generation;
    } else cancelRecording(); // Terminal/finalize drains asynchronously, including after quit.
}
void DebugSpectateRuntime::cancelPopupPreparation() {
    if (m_launched) return; // LevelInfo Back/Cancel remains an explicit cancellation.
    cancelPreparation("explicit popup cancel/back");
}
void DebugSpectateRuntime::cancelPreparation(char const* reason) {
    if (m_mode == DebugSpectateMode::PreparingRecord || m_mode == DebugSpectateMode::PreparingPlayback)
        log::info("[DebugSpectate] preparation cancelled reason={} id={}", reason, m_id);
    if ((m_mode == DebugSpectateMode::PreparingRecord || m_mode == DebugSpectateMode::PreparingPlayback) && m_token.empty()) {
        ++m_generation; m_mode = DebugSpectateMode::None; m_busy = false; return;
    }
    if (m_mode == DebugSpectateMode::PreparingPlayback) { closePlayback(); m_mode = DebugSpectateMode::None; }
    else if (m_mode == DebugSpectateMode::PreparingRecord) { m_capture.cancel(); m_mode = DebugSpectateMode::FinalizingRecord; pumpRecording(); }
}
void DebugSpectateRuntime::fail(std::string message) {
    if (m_mode == DebugSpectateMode::Playback || m_mode == DebugSpectateMode::PreparingPlayback) closePlayback();
    m_mode = DebugSpectateMode::None; m_status = std::move(message); m_busy = false;
    m_token.clear(); m_contextId.clear(); ++m_generation;
    log::warn("[DebugSpectate] {}", m_status);
}
} // namespace corum::ranked::debug
#endif
