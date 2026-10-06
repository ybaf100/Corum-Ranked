#include "SpectatorWorldDriver.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/PlayLayer.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace corum::ranked {
bool SpectatorWorldDriver::apply(PlayLayer* layer, bool authorizedDummy,
                               std::optional<SpectatorRemoteFrame> const& frame) {
    if (!authorizedDummy || !layer || layer->m_isTestMode || layer->m_isPracticeMode ||
        !layer->m_level || !layer->m_player1 || layer->m_player1->m_gameLayer != layer || !frame || !frame->pose ||
        frame->context.levelId != static_cast<int>(layer->m_level->m_levelID) ||
        frame->context.matchId.empty() || frame->context.attemptId.empty() ||
        frame->context.roundNumber <= 0 || frame->context.deathmatchSequence < 0 ||
        frame->context.generation == 0 || frame->context.epoch == 0 ||
        frame->sequence < 0 || !std::isfinite(frame->publisherElapsedMs) || frame->publisherElapsedMs < 0 ||
        !finiteSpectatorPose(*frame->pose) || frame->pose->dual) return false;
    auto const sameContext = m_frame && m_frame->context == frame->context && m_player == layer->m_player1;
    if (sameContext && (frame->sequence < m_frame->sequence || frame->publisherElapsedMs < m_frame->publisherElapsedMs)) return false;
    auto const& pose = *frame->pose;
    auto const newBoundary = !sameContext || frame->sequence != m_frame->sequence;
    auto const snap = (sameContext && frame->presentationSegment != m_frame->presentationSegment) ||
        (newBoundary && pose.discontinuity && finiteSpectatorSnap(*pose.discontinuity) &&
         (!sameContext || verifiedSpectatorSnap(*m_frame->pose, pose)));
    auto const deltaMs = sameContext ? frame->publisherElapsedMs - m_frame->publisherElapsedMs : 0;
    if (!sameContext && frame->sequence > 0)
        log::info("Spectator world history partial: firstSequence={} elapsedMs={} (native catch-up requires device verification)",
            frame->sequence, frame->publisherElapsedMs);
    m_previous = sameContext && !snap ? m_frame->pose : frame->pose;
    m_player = layer->m_player1; m_frame = frame;
    write(layer, !sameContext || snap);
    // GD 2.2081 updateSpecial ONLY writes Follow Player Y's 200-entry history
    // (official inline binding). Call its native 10ms sampler, never update().
    // Drive it by publisher time, so render FPS / HTTP batches cannot change it.
    if (sameContext && !snap && deltaMs > 0) {
        m_followRemainderMs += deltaMs;
        auto const steps = std::min(200, static_cast<int>((m_followRemainderMs + 0.000001) / 10));
        auto const startRemainder = m_followRemainderMs - deltaMs;
        ScopedSpectatorWorldDriverWrite scope(m_player);
        for (int i = 0; i < steps; ++i) {
            auto const t = std::clamp(((i + 1) * 10.0 - startRemainder) / deltaMs, 0.0, 1.0);
            m_player->setPosition({static_cast<float>(m_previous->x + (pose.x - m_previous->x) * t),
                                  static_cast<float>(m_previous->y + (pose.y - m_previous->y) * t)});
            m_player->updateSpecial(0.01f);
        }
        m_followRemainderMs = std::max(0.0, m_followRemainderMs - steps * 10);
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
        snapshotFollowHistory();
#endif
        write(layer, false);
    }
    return true;
}

void SpectatorWorldDriver::write(PlayLayer* layer, bool resetHistory) {
    if (!layer || layer->m_player1 != m_player || !m_frame || !m_frame->pose) return;
    auto const& pose = *m_frame->pose;
    ScopedSpectatorWorldDriverWrite scope(m_player);
    // Presentation flags, not portal/physics helpers. Apply discrete state
    // BEFORE the destination, particularly at a Spider gravity/snap boundary.
    m_player->m_isShip = pose.mode == SpectatorGameMode::Ship;
    m_player->m_isBall = pose.mode == SpectatorGameMode::Ball;
    m_player->m_isBird = pose.mode == SpectatorGameMode::UFO;
    m_player->m_isDart = pose.mode == SpectatorGameMode::Wave;
    m_player->m_isRobot = pose.mode == SpectatorGameMode::Robot;
    m_player->m_isSpider = pose.mode == SpectatorGameMode::Spider;
    m_player->m_isSwing = pose.mode == SpectatorGameMode::Swing;
    m_player->m_isUpsideDown = pose.upsideDown;
    m_player->m_vehicleSize = pose.mini ? 0.6f : 1.0f;
    if (m_player->m_mainLayer) m_player->m_mainLayer->setScaleY(pose.flipped ? -1.0f : 1.0f);
    m_player->m_playEffects = false; m_player->m_isHidden = true;
    m_player->m_isDead = false; // remote death is renderer-only, never local death
    m_player->setPosition({pose.x, pose.y}); // native setPosition dirties native bounds/coordinates
    m_player->m_positionX = pose.x; m_player->m_positionY = pose.y;
    m_player->setRotation(pose.rotation);
    // GameObject::m_lastPosition is also read by native saved-position/follow
    // commands. Preserve continuous history, reset at a verified destination.
    m_player->m_lastPosition = CCPoint{m_previous->x, m_previous->y};
    if (resetHistory) {
        m_player->m_playerFollowFloats.assign(200, pose.y);
        m_player->m_followRelated = 0; m_player->m_unk838 = 0;
        m_followRemainderMs = 0;
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
        snapshotFollowHistory();
#endif
    }
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
    else restoreFollowHistory();
#endif
    m_player->setVisible(false);
}

#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
void SpectatorWorldDriver::snapshotFollowHistory() {
    if (!m_player || m_player->m_playerFollowFloats.size() != m_followHistory.size()) return;
    std::copy_n(m_player->m_playerFollowFloats.begin(), m_followHistory.size(), m_followHistory.begin());
    m_followIndex = m_player->m_followRelated;
    m_followAccumulator = m_player->m_unk838;
    m_hasFollowHistory = true;
}

void SpectatorWorldDriver::restoreFollowHistory() {
    if (!m_player || !m_hasFollowHistory) return;
    // Only the exact owned dummy. Native frame/sub-step observations may write
    // its sampler, but cannot advance the publisher-time Follow-Y history.
    if (m_player->m_playerFollowFloats.size() != m_followHistory.size())
        m_player->m_playerFollowFloats.assign(m_followHistory.begin(), m_followHistory.end());
    else std::copy(m_followHistory.begin(), m_followHistory.end(), m_player->m_playerFollowFloats.begin());
    m_player->m_followRelated = m_followIndex;
    m_player->m_unk838 = m_followAccumulator;
}
#endif

void SpectatorWorldDriver::reconcile(PlayLayer* layer) { write(layer, false); }
void SpectatorWorldDriver::reset() {
    m_player = nullptr; m_frame.reset(); m_previous.reset(); m_followRemainderMs = 0;
#if defined(CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION)
    m_hasFollowHistory = false; m_followIndex = 0; m_followAccumulator = 0;
#endif
}
} // namespace corum::ranked
