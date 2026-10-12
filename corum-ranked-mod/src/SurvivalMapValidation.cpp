#include "SurvivalMapValidation.hpp"
#include <Geode/binding/GJGameLevel.hpp>

namespace corum::ranked {
static_assert(static_cast<int>(DemonDifficultyType::HardDemon)==0);
static_assert(static_cast<int>(DemonDifficultyType::EasyDemon)==3);
static_assert(static_cast<int>(DemonDifficultyType::MediumDemon)==4);
static_assert(static_cast<int>(DemonDifficultyType::InsaneDemon)==5);
static_assert(static_cast<int>(DemonDifficultyType::ExtremeDemon)==6);

bool survivalNativeMapMatches(GJGameLevel* level, SurvivalMapMetadata const& selected,
    SurvivalDifficultyGroup group, SurvivalMapPhase phase, int minimumLevelId) {
    if(!level || level->m_levelType != GJLevelType::Saved || level->m_levelString.empty() || level->m_levelNotDownloaded) return false;
    auto const native = SurvivalMapMetadata{level->m_levelID.value(),level->m_stars.value(),level->m_levelVersion,
        level->m_demon.value() ? survivalDemonFromNativeCode(level->m_demonDifficulty) : SurvivalDemon::None,
        level->m_autoLevel,level->isPlatformer()};
    return native == selected && survivalMapAllowed(native,group,phase,minimumLevelId);
}
} // namespace corum::ranked
