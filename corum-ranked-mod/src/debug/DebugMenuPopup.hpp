#pragma once
#include "../domain/RankedMode.hpp"
namespace corum::ranked::debug {
void showDebugMenuPopup(RankedMode mode=RankedMode::SoloRanked);
void showDebugAdminPopup(RankedMode mode=RankedMode::SoloRanked);
}
