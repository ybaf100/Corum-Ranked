#pragma once

#include <cstdint>
#include <string>

namespace corum::ranked {

struct LevelPreparationContext {
    std::string matchId;
    int levelId = 0;
    int round = 0;
    int deathmatch = 0;
    bool placement = false;
    int stage = 0;
    int map = 0;
    bool operator==(LevelPreparationContext const&) const = default;
    bool valid() const { return !matchId.empty() && levelId > 0; }
};

// Snapshot values: a retained GD pointer alone cannot detect in-place mutation.
// Secondary songIDs are deliberately excluded; native geometry acquisition can
// populate them after the online list metadata has arrived.
struct LevelMetadata {
    int levelId = 0;
    std::string name;
    std::string creator;
    int userId = 0;
    int accountId = 0;
    int songId = 0;
    int audioTrack = 0;
    int version = 0;
    bool online = false;
    bool operator==(LevelMetadata const&) const = default;
    bool complete() const {
        // Zero account/user IDs and built-in track 0 are not missing metadata.
        // Completeness is necessary, but only a native online lookup may attest it.
        return online && levelId > 0 && !name.empty() && !creator.empty() &&
            songId >= 0 && audioTrack >= 0;
    }
};

class PreparedLevelProof {
public:
    LevelPreparationContext const& context() const { return m_context; }
    void clear() { m_context = {}; m_metadata = {}; }
    // Call ONLY for the accepted native online lookup, never a saved-cache hit.
    bool attest(LevelPreparationContext const& context, LevelMetadata const& metadata) {
        clear();
        if (!context.valid() || context.levelId != metadata.levelId || !metadata.complete()) return false;
        m_context = context;
        m_metadata = metadata;
        return true;
    }
    bool matches(LevelPreparationContext const& current, LevelMetadata const& live) const {
        return current.valid() && m_context == current && m_metadata.complete() && m_metadata == live;
    }
private:
    LevelPreparationContext m_context;
    LevelMetadata m_metadata;
};

// Native list delegates carry a search key, NOT a request generation. Never
// relabel an in-flight response or overlap same-key timeout retries. An obsolete
// ticket must drain even if the desired context changes A -> B -> A.
class LevelLookupTicket {
public:
    bool busy() const { return m_busy; }
    std::uint64_t generation() const { return m_generation; }
    LevelPreparationContext const& context() const { return m_context; }
    bool begin(LevelPreparationContext const& context) {
        if (m_busy || !context.valid()) return false;
        m_context = context;
        ++m_generation;
        m_busy = true;
        m_discarded = false;
        return true;
    }
    void observe(LevelPreparationContext const& current) {
        if (m_busy && m_context != current) discard();
    }
    void discard() { m_discarded = true; }
    bool finish(LevelPreparationContext const& current) {
        if (!m_busy) return false;
        m_busy = false;
        return !m_discarded && m_context == current;
    }
private:
    LevelPreparationContext m_context;
    std::uint64_t m_generation = 0;
    bool m_busy = false;
    bool m_discarded = false;
};

inline bool preparationAllowsLaunch(bool currentContext, bool metadataReady,
    bool geometryReady, bool songReady, bool songBypass, bool startAllowed,
    bool modalOpen, bool nativeBusy) {
    return currentContext && metadataReady && geometryReady &&
        (songReady || songBypass) && startAllowed && !modalOpen && !nativeBusy;
}

} // namespace corum::ranked
