#pragma once
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>

namespace corum::ranked {

/** P0-B reason classes. Status (SpectatorLiveStatus) stays the coarse UI state;
 * the class names the recovery policy for the exact reason string. */
enum class SpectatorFailureClass {
    None,
    Buffering,          // stream identity valid; latest visuals late
    HistoryGap,         // HISTORY_GAP / HISTORY_WINDOW_EXPIRED / ATTEMPT_START_MISSING
    WorldDesync,        // native world/random/collision evidence mismatch
    RecaptureRequired,  // this generation cannot produce proof; wait for a NEW Attempt/generation
    Disconnected,       // auth/transport/retry exhaustion only
    Waiting,            // no eligible stream yet (no viewers, paused, practice)
};

inline SpectatorFailureClass spectatorFailureClass(std::string_view reason) {
    if (reason.empty()) return SpectatorFailureClass::None;
    // A complete server history replayed natively is still reported partial
    // (engine/RNG parity is unproven) but it is a declared limit, not a gap.
    if (reason == "NATIVE_HISTORY_REPLAY_LIMITED") return SpectatorFailureClass::None;
    auto has = [&](std::string_view needle) { return reason.find(needle) != std::string_view::npos; };
    if (reason == "NETWORK_RETRY_EXHAUSTED" || has("UNAUTHORIZED") || has("FORBIDDEN") || reason == "HTTP_401" || reason == "HTTP_403" ||
        reason == "SPECTATOR_NOT_ELIGIBLE")
        return SpectatorFailureClass::Disconnected;
    if (reason == "POSE_DISCONTINUITY" || reason == "PLAYER2_POSE_DISCONTINUITY" || reason == "INVALID_POSE" ||
        reason == "DUAL_UNSUPPORTED" || reason == "UNSUPPORTED_PROTOCOL_VERSION" || reason == "RESTORE_RETRY_EXHAUSTED")
        return SpectatorFailureClass::RecaptureRequired;
    if (has("RANDOM") || has("WORLD") || has("COLLISION") || has("DESYNC") || has("TRIGGER") || has("FOLLOW") ||
        reason == "HISTORY_LEVEL_MISMATCH")
        return SpectatorFailureClass::WorldDesync;
    if (has("HISTORY") || has("START_MISSING") || has("SEQUENCE_GAP") || has("RESTORE") || reason == "INVALID_RESTORE_DESCRIPTOR")
        return SpectatorFailureClass::HistoryGap;
    if (has("TIMEOUT") || has("STALE") || has("BUFFER"))
        return SpectatorFailureClass::Buffering;
    if (has("WAITING") || has("NO_VIEWERS") || has("PRACTICE") || has("PAUSE") || has("CONTEXT_ENDED") || has("NOT_STARTED"))
        return SpectatorFailureClass::Waiting;
    return SpectatorFailureClass::HistoryGap;
}

inline char const* spectatorFailureClassName(SpectatorFailureClass c) {
    switch (c) {
        case SpectatorFailureClass::None: return "NONE";
        case SpectatorFailureClass::Buffering: return "BUFFERING";
        case SpectatorFailureClass::HistoryGap: return "HISTORY_GAP";
        case SpectatorFailureClass::WorldDesync: return "WORLD_DESYNC";
        case SpectatorFailureClass::RecaptureRequired: return "RECAPTURE_REQUIRED";
        case SpectatorFailureClass::Disconnected: return "DISCONNECTED";
        case SpectatorFailureClass::Waiting: return "WAITING";
    }
    return "NONE";
}

/** Identity of one unrecoverable viewer failure. viewerEpoch and
 * firstFailureSequence are diagnostics only; the suppression key is the
 * immutable evidence identity (session, match, context, attempt, generation),
 * so a scene re-entry or local viewer reset cannot re-run the same proof. */
struct SpectatorFailureFingerprint {
    std::string session, match, context, attemptId;
    std::uint64_t streamGeneration = 0, viewerEpoch = 0;
    std::string reason;
    std::int64_t firstFailureSequence = -1;
    std::string key() const {
        return session + '\x1f' + match + '\x1f' + context + '\x1f' + attemptId + '\x1f' + std::to_string(streamGeneration);
    }
};

/** Bounded memory of rejected evidence identities. A new Attempt or a new
 * stream generation is a different key, which is the only way to retry. */
class SpectatorFailureLedger {
public:
    static constexpr std::size_t kCapacity = 64;
    void record(SpectatorFailureFingerprint fingerprint) {
        auto const key = fingerprint.key();
        for (auto const& item : m_items) if (item.key() == key) return; // first failure is retained
        if (m_items.size() >= kCapacity) m_items.pop_front();
        m_items.push_back(std::move(fingerprint));
        ++m_recorded;
    }
    std::optional<SpectatorFailureFingerprint> find(std::string const& session, std::string const& match, std::string const& context,
                                                    std::string const& attemptId, std::uint64_t generation) const {
        SpectatorFailureFingerprint probe;
        probe.session = session; probe.match = match; probe.context = context; probe.attemptId = attemptId; probe.streamGeneration = generation;
        auto const key = probe.key();
        for (auto const& item : m_items) if (item.key() == key) return item;
        return std::nullopt;
    }
    void noteSuppressed() { ++m_suppressed; }
    std::size_t size() const { return m_items.size(); }
    std::uint64_t recorded() const { return m_recorded; }
    std::uint64_t suppressed() const { return m_suppressed; }
    void clear() { m_items.clear(); }
private:
    std::deque<SpectatorFailureFingerprint> m_items;
    std::uint64_t m_recorded = 0, m_suppressed = 0;
};

} // namespace corum::ranked
