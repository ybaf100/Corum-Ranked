#pragma once

#include "domain/MapDifficulty.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/LevelManagerDelegate.hpp>
#include <chrono>
#include <map>
#include <set>

namespace corum::ranked {
// Appearance-only metadata. No object retained, geometry, readiness or score authority.
// Uses the existing keyed GD response hook; does not claim the global delegate in flight.
class RankedMapDetails final : public LevelManagerDelegate {
public:
    static RankedMapDetails& get();
    void request(std::vector<int> ids);
    void remember(GJGameLevel* level);
    MapDifficulty difficulty(int levelId) const;
    std::uint64_t revision() const { return m_revision; }
    std::uint64_t requestGeneration(char const* key) const;
    void responseCompleted(char const* key, std::uint64_t generation);
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key) override;
    void loadLevelsFinished(cocos2d::CCArray* levels, char const* key, int) override;
    void loadLevelsFailed(char const* key) override;
    void loadLevelsFailed(char const* key, int) override;
    void setupPageInfo(gd::string, char const*) override {}
private:
    struct Cached { MapDifficulty difficulty; std::chrono::steady_clock::time_point fetchedAt; };
    std::map<int, Cached> m_cache;
    std::set<int> m_wanted, m_pending;
    std::string m_key;
    bool m_busy = false;
    std::uint64_t m_generation = 0, m_revision = 0;
    std::chrono::steady_clock::time_point m_retryAt {};
};
} // namespace corum::ranked
