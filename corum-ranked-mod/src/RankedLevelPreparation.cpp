#include "RankedLevelPreparation.hpp"
#include "RankedRuntime.hpp"
#include "SurvivalRuntime.hpp"
#include "RankedMapDetails.hpp"
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
#include "debug/DebugSpectateRuntime.hpp"
#endif
#include <Geode/binding/CustomSongWidget.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>
#include <Geode/modify/GameLevelManager.hpp>

using namespace geode::prelude;

namespace corum::ranked {

LevelPreparationContext currentPreparationContext() {
    auto const survivalContext=SurvivalRuntime::get().preparationContext();
    if(survivalContext.valid())return survivalContext;
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
    auto const debugContext = debug::DebugSpectateRuntime::get().preparationContext();
    if (debugContext.valid()) return debugContext;
#endif
    auto const& runtime = RankedRuntime::get();
    auto const& view = runtime.view();
    if (view.stage != RuntimeStage::Matched) return {};
    return {view.match.matchId, runtime.currentLevelId(), view.match.roundNumber,
        view.match.deathmatchSequence, view.match.placement,
        view.match.placement ? view.placement.currentStageIndex : 0,
        view.match.placement ? view.placement.currentMapIndex : 0};
}

std::string preparationContextText(LevelPreparationContext const& context) {
    return fmt::format("{}:{}:R{}:D{}:P{}:{}:{}", context.matchId, context.levelId,
        context.round, context.deathmatch, context.placement, context.stage, context.map);
}

LevelMetadata levelMetadata(GJGameLevel* level) {
    if (!level) return {};
    return {static_cast<int>(level->m_levelID), std::string(level->m_levelName.c_str()),
        std::string(level->m_creatorName.c_str()), static_cast<int>(level->m_userID),
        static_cast<int>(level->m_accountID), level->m_songID, level->m_audioTrack,
        level->m_levelVersion,
        level->m_levelType == GJLevelType::Saved || level->m_levelType == GJLevelType::SearchResult};
}

void tracePreparedLevel(char const* event, GJGameLevel* level, CustomSongWidget* widget, void const* owner) {
    auto const metadata = levelMetadata(level);
    log::info("[RankedPrepare] {} context={} owner={} level={} id={} name='{}' creator='{}' "
        "user={} account={} version={} geometryBytes={} notDownloaded={} type={} song={} track={} "
        "songs='{}' widget={} widgetSong={} robtop={} metadataComplete={}",
        event, preparationContextText(currentPreparationContext()), owner, static_cast<void*>(level),
        metadata.levelId, metadata.name, metadata.creator, metadata.userId, metadata.accountId,
        metadata.version, level ? level->m_levelString.size() : 0, !level || level->m_levelNotDownloaded,
        level ? static_cast<int>(level->m_levelType) : -1, metadata.songId, metadata.audioTrack,
        level ? std::string(level->m_songIDs.c_str()) : "", static_cast<void*>(widget),
        widget ? widget->m_customSongID : 0, widget && widget->m_isRobtopSong, metadata.complete());
}

RankedLevelPreparation& RankedLevelPreparation::get() {
    // A native HTTP response can outlive every Ranked UI node.
    static auto* instance = new RankedLevelPreparation;
    return *instance;
}

bool RankedLevelPreparation::authorizes(GJGameLevel* level, LevelPreparationContext const& context) const {
    return level && m_level && context.valid() && context == currentPreparationContext() &&
        m_proof.matches(context, levelMetadata(level));
}

GJGameLevel* RankedLevelPreparation::level() const {
    return authorizes(m_level.data(), currentPreparationContext()) ? m_level.data() : nullptr;
}

bool RankedLevelPreparation::adopt(GJGameLevel* level, LevelPreparationContext const& context) {
    if (!authorizes(level, context)) return false;
    // Follow a verified native replacement instead of retaining an older list
    // pointer forever. The value proof does not change on pointer replacement.
    m_level = level;
    if (!level->m_levelString.empty() && !level->m_levelNotDownloaded) m_waitContext = {};
    return true;
}

bool RankedLevelPreparation::adoptVerifiedSavedLevel(LevelPreparationContext const& context) {
    // Re-query AFTER online attestation: native download may have replaced the
    // cached pointer while the exact lookup was in flight. Retain the actual
    // Saved object, never copy geometry into a SearchResult or synthesize a level.
    if (!authorizes(m_level.data(), context)) return false;
    auto* manager = GameLevelManager::sharedState();
    auto* saved = manager ? manager->getSavedLevel(context.levelId) : nullptr;
    if (!saved || saved->m_levelType != GJLevelType::Saved ||
        saved->m_levelString.empty() || saved->m_levelNotDownloaded ||
        !authorizes(saved, context)) return false;
    return adopt(saved, context);
}

void RankedLevelPreparation::invalidate(LevelPreparationContext const& context) {
    if (m_ticket.busy() && m_ticket.context() == context) m_ticket.discard();
    if (m_proof.context() != context) return;
    m_level = nullptr;
    m_proof.clear();
    m_nextAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

void RankedLevelPreparation::observeContext() {
    auto const current = currentPreparationContext();
    m_ticket.observe(current);
    if (m_proof.context() != current) {
        m_proof.clear();
        m_level = nullptr;
    }
}

void RankedLevelPreparation::poll() {
    observeContext();
    auto const current = currentPreparationContext();
    if (!current.valid() || level()) return;
    auto* manager = GameLevelManager::sharedState();
    if (!manager) return;
    auto const now = std::chrono::steady_clock::now();
    if (m_waitContext != current) {
        m_waitContext = current;
        m_waitStartedAt = now;
        m_timeoutReported = false;
        m_nextAttempt = {};
    }
    // Account for the entire wait, including incomplete responses and retries.
    // The singleton GD delegate is intentionally NOT part of lookup ownership:
    // another screen/mod may legitimately hold it while this exact keyed query
    // runs. Completion temporarily routes only our matching tag to this resolver.
    if (now - m_waitStartedAt >= std::chrono::seconds(30) && !m_timeoutReported) {
        m_timeoutReported = true;
        m_error = "GD METADATA TIMEOUT - RETRYING WHEN AVAILABLE";
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
        if (!debug::DebugSpectateRuntime::get().preparationContext().valid())
#endif
        if(SurvivalRuntime::get().preparationContext().valid())SurvivalRuntime::get().preparationFailed("NATIVE_METADATA_TIMEOUT");
        else RankedRuntime::get().reportMapDownloadFailure();
    }
    if (m_ticket.busy()) {
        // Reissuing after a local timeout would let a late same-key callback be
        // mistaken for the next request. Native HTTP owns completion/cancellation.
        return;
    }
    if (now < m_nextAttempt) return;
    auto* search = GJSearchObject::create(SearchType::Type19,
        fmt::format("{}&gameVersion=22", current.levelId));
    if (!search || !search->getKey()) return;
    auto const key = std::string(search->getKey());
    if (manager->isDLActive(key.c_str())) return;

    // Expire ONLY this exact online query. Do not remove saved geometry, user
    // options or statistics. A successful response will repopulate GD's cache.
    if (manager->m_storedLevels) manager->m_storedLevels->removeObjectForKey(key.c_str());
    manager->resetTimerForKey(key.c_str());
    if (!m_ticket.begin(current)) return;
    m_key = key;
    m_level = nullptr;
    m_error.clear();
    tracePreparedLevel("cache-before-lookup", manager->getSavedLevel(current.levelId));
    log::info(
        "[RankedPrepare] lookup generation={} request={} key={} foreignDelegate={}",
        m_ticket.generation(), preparationContextText(current), m_key,
        static_cast<void*>(manager->m_levelManagerDelegate)
    );
    manager->getOnlineLevels(search);
}

std::uint64_t RankedLevelPreparation::requestGeneration(char const* key) const {
    return m_ticket.busy() && key && m_key == key ? m_ticket.generation() : 0;
}

bool RankedLevelPreparation::finish() {
    auto* manager = GameLevelManager::sharedState();
    if (manager && manager->m_levelManagerDelegate == this) manager->m_levelManagerDelegate = nullptr;
    auto const accepted = m_ticket.finish(currentPreparationContext());
    log::info("[RankedPrepare] lookup-finished generation={} request={} current={} accepted={}",
        m_ticket.generation(), preparationContextText(m_ticket.context()),
        preparationContextText(currentPreparationContext()), accepted);
    m_key.clear();
    return accepted;
}

void RankedLevelPreparation::responseCompleted(char const* key, std::uint64_t generation) {
    if (!generation || requestGeneration(key) != generation) return;
    // If native parsing did not dispatch one of our LevelManagerDelegate methods,
    // drain the ticket after GD processes the response and retry. Matching
    // callbacks normally finish the ticket before this fallback is reached.
    m_ticket.discard();
    finish();
    fail("GD LOOKUP CALLBACK MISSING - RETRYING");
}

void RankedLevelPreparation::fail(char const* message) {
    m_error = message;
    m_nextAttempt = std::chrono::steady_clock::now() + std::chrono::milliseconds(1400);
}

void RankedLevelPreparation::loadLevelsFinished(CCArray* levels, char const* key) {
    if (!requestGeneration(key)) return;
    Ref<GJGameLevel> exact;
    if (levels) {
        for (unsigned int i = 0; i < levels->count(); ++i) {
            auto* candidate = typeinfo_cast<GJGameLevel*>(levels->objectAtIndex(i));
            if (candidate && static_cast<int>(candidate->m_levelID) == m_ticket.context().levelId) {
                exact = candidate;
                break;
            }
        }
    }
    tracePreparedLevel("online-result", exact.data());
    if (!finish()) return;
    auto const metadata = levelMetadata(exact.data());
    if (!m_proof.attest(currentPreparationContext(), metadata)) {
        fail("INCOMPLETE GD METADATA - RETRYING");
        return;
    }
    m_level = exact;
    RankedMapDetails::get().remember(exact.data());
    m_error.clear();
}

void RankedLevelPreparation::loadLevelsFinished(CCArray* levels, char const* key, int) {
    loadLevelsFinished(levels, key);
}
void RankedLevelPreparation::loadLevelsFailed(char const* key) {
    if (requestGeneration(key) && finish()) fail("GD LEVEL LOOKUP FAILED - RETRYING");
}
void RankedLevelPreparation::loadLevelsFailed(char const* key, int) { loadLevelsFailed(key); }

} // namespace corum::ranked

class $modify(CorumRankedMetadataResponse, GameLevelManager) {
    void onGetOnlineLevelsCompleted(gd::string response, gd::string tag) {
        auto& preparation = corum::ranked::RankedLevelPreparation::get();
        auto const generation = preparation.requestGeneration(tag.c_str());
        if (!generation) {
            auto& details = corum::ranked::RankedMapDetails::get();
            auto const visualGeneration = details.requestGeneration(tag.c_str());
            if (visualGeneration) {
                auto* previousDelegate = m_levelManagerDelegate;
                m_levelManagerDelegate = &details;
                GameLevelManager::onGetOnlineLevelsCompleted(response, tag);
                if (m_levelManagerDelegate == &details || m_levelManagerDelegate == nullptr)
                    m_levelManagerDelegate = previousDelegate;
                details.responseCompleted(tag.c_str(), visualGeneration);
                return;
            }
            GameLevelManager::onGetOnlineLevelsCompleted(response, tag);
            return;
        }

        // GameLevelManager exposes one singleton LevelManagerDelegate even though
        // HTTP requests are independently keyed. Do not reserve/steal that slot
        // while our request is in flight. Only for this exact matching response,
        // route native parsing to the persistent Ranked resolver, then restore
        // whichever foreign delegate still owns the singleton slot.
        auto* previousDelegate = m_levelManagerDelegate;
        m_levelManagerDelegate = &preparation;
        GameLevelManager::onGetOnlineLevelsCompleted(response, tag);
        if (m_levelManagerDelegate == &preparation || m_levelManagerDelegate == nullptr) {
            m_levelManagerDelegate = previousDelegate;
        } else if (m_levelManagerDelegate != previousDelegate) {
            log::warn(
                "[RankedPrepare] GD installed a new level delegate during exact lookup; preserving it"
            );
        }
        preparation.responseCompleted(tag.c_str(), generation);
    }
};

