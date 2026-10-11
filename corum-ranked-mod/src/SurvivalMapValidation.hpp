#pragma once
#include "domain/SurvivalMapPolicy.hpp"
class GJGameLevel;

namespace corum::ranked {
// M4 preparation/PlayLayer adapters use the server's selected metadata here;
// each client validates the native downloaded object, never chooses its own ID.
bool survivalNativeMapMatches(GJGameLevel* level, SurvivalMapMetadata const& selected,
    SurvivalDifficultyGroup group, SurvivalMapPhase phase, int minimumLevelId = 40000000);
}
