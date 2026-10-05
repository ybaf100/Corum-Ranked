#include "DebugSpectateRuntime.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "../RankedRuntime.hpp"
#include "../RankedLevelPreparation.hpp"
#include "../SpectatorLiveRuntime.hpp"
#include <Geode/binding/LevelInfoLayer.hpp>

using namespace geode::prelude;
namespace {
constexpr std::string_view kServer = "https://corum-ranked.onrender.com";
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
void DebugSpectateRuntime::start(bool playback, std::string password) {
    auto& ranked = RankedRuntime::get();
    if (m_mode != DebugSpectateMode::None || m_busy || ranked.view().stage != RuntimeStage::Ready ||
        ranked.hasLocalGameplayAuthority() || ranked.spectatorLiveSessionToken().empty()) {
        m_status = "RETURN TO RANKED LOBBY / WAIT FOR CURRENT DEBUG SESSION"; return;
    }
    installPump();
    m_mode = playback ? DebugSpectateMode::PreparingPlayback : DebugSpectateMode::PreparingRecord;
    m_status = playback ? "REQUESTING SERVER PLAYBACK..." : "CREATING SERVER RECORDING...";
    m_launched = false; m_playbackComplete = false; m_errors = 0; m_capture = {};
    m_id.clear(); m_token.clear(); m_contextId.clear();
    // Admission belongs to a NEW scope. A deadline/backoff left by an expired
    // or failed scope must not cancel this request before its HTTP ACK arrives.
    m_expireAt = {}; m_nextUploadAt = {}; m_recordStartedAt = {};
    m_prepareStartedAt = std::chrono::steady_clock::now();
    matjson::Value body;
    body["password"] = password; body["levelId"] = kDebugSpectateLevelId; body["formatVersion"] = kDebugSpectateFormatVersion;
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
        if (root["levelId"].asInt().unwrapOr(0) != kDebugSpectateLevelId ||
            root["formatVersion"].asInt().unwrapOr(0) != kDebugSpectateFormatVersion) { fail("INVALID DEBUG SERVER CONTEXT"); return; }
        m_id = root[playback ? "playbackId" : "recordingId"].asString().unwrapOr("");
        m_token = root["token"].asString().unwrapOr("");
        m_contextId = playback ? root["contextId"].asString().unwrapOr("") : "debug-record:" + m_id;
        if (m_id.empty() || m_token.empty() || m_contextId.empty()) { fail("MISSING DEBUG SERVER SCOPE"); return; }
        m_expireAt = m_prepareStartedAt + std::chrono::minutes(15);
        m_status = "PREPARING VOICE - VERIFIED GD METADATA / VANILLA DOWNLOAD";
        log::info("[DebugSpectate] {} created id={}", playback ? "playback" : "recording", m_id);
    });
}
bool DebugSpectateRuntime::preparedLevel(int levelId) const {
    return RankedRuntime::get().view().stage == RuntimeStage::Ready && levelId == kDebugSpectateLevelId && !m_token.empty() &&
        (m_mode == DebugSpectateMode::PreparingRecord || m_mode == DebugSpectateMode::PreparingPlayback);
}
bool DebugSpectateRuntime::recordLevel(int levelId) const {
    return levelId == kDebugSpectateLevelId && !m_token.empty() &&
        (m_mode == DebugSpectateMode::Recording || m_mode == DebugSpectateMode::FinalizingRecord);
}
bool DebugSpectateRuntime::playbackLevel(int levelId) const {
    return RankedRuntime::get().view().stage == RuntimeStage::Ready && levelId == kDebugSpectateLevelId &&
        !m_token.empty() && m_mode == DebugSpectateMode::Playback;
}
LevelPreparationContext DebugSpectateRuntime::preparationContext() const {
    if (m_token.empty() || m_contextId.empty() || m_mode == DebugSpectateMode::None) return {};
    return {m_contextId, kDebugSpectateLevelId, 1, 0, false, 0, 0};
}
void DebugSpectateRuntime::tick() {
    auto const now = std::chrono::steady_clock::now();
    if (m_mode == DebugSpectateMode::None) return;
    if (m_expireAt.time_since_epoch().count() && now >= m_expireAt) {
        fail("DEBUG SPECTATE SESSION EXPIRED"); return;
    }
    if (m_mode == DebugSpectateMode::Recording || m_mode == DebugSpectateMode::FinalizingRecord) { pumpRecording(); return; }
    if (RankedRuntime::get().view().stage != RuntimeStage::Ready) { cancelPreparation(); return; }
    if (m_mode == DebugSpectateMode::Playback) return;
    if (now - m_prepareStartedAt > std::chrono::minutes(2)) { cancelPreparation(); m_status = "VOICE PREPARATION TIMEOUT"; return; }
    if (!preparedLevel(kDebugSpectateLevelId) || m_launched || m_busy) return;
    auto& preparation = RankedLevelPreparation::get(); preparation.poll();
    auto* level = preparation.level();
    if (!level || !preparation.authorizes(level, preparationContext())) return;
    auto* scene = LevelInfoLayer::scene(level, false);
    if (!scene) { fail("CANNOT OPEN VERIFIED VOICE LEVELINFO"); return; }
    m_launched = true;
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
            "/api/ranked/debug/spectate/playbacks/" + m_id + "/spectator-live");
    }
}
void DebugSpectateRuntime::recordPose(SpectatorGhostPose const& pose, bool terminal) {
    if (m_mode != DebugSpectateMode::Recording || m_capture.ended()) return;
    auto const elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_recordStartedAt).count();
    m_capture.capture(elapsed, pose, terminal);
    if (m_capture.ended()) m_mode = DebugSpectateMode::FinalizingRecord;
    pumpRecording();
}
void DebugSpectateRuntime::cancelRecording() {
    if (m_mode != DebugSpectateMode::Recording) return;
    m_capture.cancel(); m_mode = DebugSpectateMode::FinalizingRecord; pumpRecording();
}
void DebugSpectateRuntime::pumpRecording() {
    if (m_busy || std::chrono::steady_clock::now() < m_nextUploadAt || m_token.empty()) return;
    auto const batch = m_capture.batch();
    if (batch.empty() && !m_capture.ended()) return;
    auto http = request(m_token); matjson::Value body;
    if (!batch.empty()) {
        body["formatVersion"] = kDebugSpectateFormatVersion;
        auto frames = matjson::Value::array();
        for (auto const& f : batch) {
            matjson::Value frame; frame["sequence"] = f.sequence; frame["elapsedMs"] = f.elapsedMs;
            matjson::Value pose; pose["x"] = static_cast<double>(f.pose.x); pose["y"] = static_cast<double>(f.pose.y);
            pose["rotation"] = static_cast<double>(f.pose.rotation); pose["dead"] = f.pose.dead; pose["finished"] = f.pose.finished;
            frame["keyframe"] = pose; frames.push(frame);
        }
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
void DebugSpectateRuntime::cancelPreparation() {
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
