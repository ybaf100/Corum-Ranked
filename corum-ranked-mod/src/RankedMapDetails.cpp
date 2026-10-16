#include "RankedMapDetails.hpp"
#include "RankedLevelPreparation.hpp"
#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/GJSearchObject.hpp>

using namespace geode::prelude;

namespace corum::ranked {
RankedMapDetails& RankedMapDetails::get() {
    static auto* resolver = new RankedMapDetails;
    return *resolver;
}
void RankedMapDetails::remember(GJGameLevel* level) {
    if (!level || static_cast<int>(level->m_levelID) <= 0) return;
    auto const id = static_cast<int>(level->m_levelID);
    auto const value = (level->m_levelType==GJLevelType::Main && static_cast<int>(level->m_demon)!=0)
        ? MapDifficulty{"Demon","diffIcon_06_btn_001.png",6} : mapDifficulty(level->m_autoLevel, static_cast<int>(level->m_demon) != 0,
        level->m_demonDifficulty, level->getAverageDifficulty());
    MapMetadata metadata;
    auto const demon = static_cast<int>(level->m_demon) != 0;
    metadata.kind = demon ? "DEMON" : "NORMAL";
    metadata.featureScore=std::max(0,level->m_featured);metadata.epic=std::clamp(level->m_isEpic,0,3);
    auto const stars = static_cast<int>(level->m_stars);
    if (!demon && stars >= 0 && stars <= 10) metadata.stars = stars;
    // Verification/replay ticks are not a trustworthy authored playback time.
    // Actual seconds come from explicit server metadata; native gives a grade.
    if(level->m_levelLength>=0 && level->m_levelLength<=4)metadata.lengthCategory=level->m_levelLength;
    auto const previous = m_cache.find(id);
    if (previous == m_cache.end() || previous->second.difficulty != value || previous->second.metadata != metadata) ++m_revision;
    m_cache[id] = {value, std::chrono::steady_clock::now(),metadata};
    // The lifetime cache only serves UI; bound it rather than retain every map visited.
    if (m_cache.size() > 256) {
        auto oldest = std::min_element(m_cache.begin(), m_cache.end(), [](auto const& a, auto const& b) {
            return a.second.fetchedAt < b.second.fetchedAt;
        });
        m_cache.erase(oldest);
    }
}
MapDifficulty RankedMapDetails::difficulty(int id) const {
    auto const it = m_cache.find(id);
    return it == m_cache.end() ? MapDifficulty{} : it->second.difficulty;
}
MapMetadata RankedMapDetails::metadata(int id) const {
    auto const it = m_cache.find(id);
    return it == m_cache.end() ? MapMetadata{} : it->second.metadata;
}
void RankedMapDetails::request(std::vector<int> ids) {
    m_wanted.clear();
    for (auto id : ids) if (id > 0 && m_wanted.size() < 16) m_wanted.insert(id);
    auto const now = std::chrono::steady_clock::now();
    if (m_wanted.empty() || m_busy || now < m_retryAt || RankedLevelPreparation::get().busy()) return;
    std::vector<int> missing;
    auto const current = currentPreparationContext();
    for (auto id : m_wanted) {
        if (id == current.levelId && current.valid()) continue; // preparation attestation will remember it
        auto const it = m_cache.find(id);
        if (it == m_cache.end() || now - it->second.fetchedAt > std::chrono::minutes(10)) missing.push_back(id);
    }
    if (missing.empty()) return;
    std::string query;
    for (auto id : missing) { if (!query.empty()) query += ','; query += std::to_string(id); }
    // Duplicate one ID for a single-map UI query: it can never collide with the
    // exact single-ID preparation query. GD Type19 supports comma-separated IDs.
    if (missing.size() == 1) query += ',' + std::to_string(missing.front());
    auto* search = GJSearchObject::create(SearchType::Type19, query + "&gameVersion=22");
    auto* manager = GameLevelManager::sharedState();
    if (!manager || !search || !search->getKey()) return;
    auto const key = std::string(search->getKey());
    if (manager->isDLActive(key.c_str())) return;
    m_pending = std::set<int>(missing.begin(), missing.end());
    m_key = key; m_busy = true; ++m_generation;
    if (manager->m_storedLevels) manager->m_storedLevels->removeObjectForKey(key.c_str());
    manager->resetTimerForKey(key.c_str());
    manager->getOnlineLevels(search);
}
std::uint64_t RankedMapDetails::requestGeneration(char const* key) const {
    return m_busy && key && m_key == key ? m_generation : 0;
}
void RankedMapDetails::responseCompleted(char const* key, std::uint64_t generation) {
    if (requestGeneration(key) != generation || !generation) return;
    loadLevelsFailed(key);
}
void RankedMapDetails::loadLevelsFinished(CCArray* levels, char const* key) {
    if (!requestGeneration(key)) return;
    if (levels) for (auto* object : CCArrayExt<GJGameLevel*>(levels)) {
        if (object && m_pending.count(static_cast<int>(object->m_levelID))) remember(object);
    }
    m_busy = false; m_pending.clear(); m_key.clear();
    m_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(10);
}
void RankedMapDetails::loadLevelsFinished(CCArray* levels, char const* key, int) { loadLevelsFinished(levels, key); }
void RankedMapDetails::loadLevelsFailed(char const* key) {
    if (!requestGeneration(key)) return;
    m_busy = false; m_pending.clear(); m_key.clear();
    m_retryAt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
}
void RankedMapDetails::loadLevelsFailed(char const* key, int) { loadLevelsFailed(key); }
} // namespace corum::ranked
