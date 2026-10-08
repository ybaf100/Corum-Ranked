#pragma once

#include "domain/LevelPreparationPolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <chrono>

class CustomSongWidget;

namespace corum::ranked {

LevelPreparationContext currentPreparationContext();
std::string preparationContextText(LevelPreparationContext const& context);
LevelMetadata levelMetadata(GJGameLevel* level);
void tracePreparedLevel(char const* event, GJGameLevel* level,
    CustomSongWidget* widget = nullptr, void const* owner = nullptr);

// Owns both callback lifetime and the online metadata attestation, independently
// of a temporary RankedPopup or LevelInfo scene. Native GD still owns downloads.
class RankedLevelPreparation final : public LevelManagerDelegate {
public:
    static RankedLevelPreparation& get();
    void poll();
    void observeContext();
    void invalidate(LevelPreparationContext const& context);
    GJGameLevel* level() const;
    bool authorizes(GJGameLevel* level, LevelPreparationContext const& context) const;
    bool adopt(GJGameLevel* level, LevelPreparationContext const& context);
    // Online proof is required first; saved geometry never attests metadata.
    bool adoptVerifiedSavedLevel(LevelPreparationContext const& context);
    bool busy() const { return m_ticket.busy(); }
    std::string const& error() const { return m_error; }
    std::uint64_t requestGeneration(char const* key) const;
    void responseCompleted(char const* key, std::uint64_t generation);

    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int type) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFailed(char const* key, int type) override;
    void setupPageInfo(gd::string, char const*) override {}
private:
    bool finish();
    void fail(char const* message);
    LevelLookupTicket m_ticket;
    PreparedLevelProof m_proof;
    geode::Ref<GJGameLevel> m_level;
    std::string m_key;
    std::string m_error;
    std::chrono::steady_clock::time_point m_nextAttempt {};
    LevelPreparationContext m_waitContext;
    std::chrono::steady_clock::time_point m_waitStartedAt {};
    bool m_timeoutReported = false;
};

} // namespace corum::ranked
