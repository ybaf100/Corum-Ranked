#pragma once

#include <Geode/Geode.hpp>

namespace corum::ranked {

// Opens the real Geometry Dash LevelInfoLayer for the selected Ranked map.
// LevelInfoLayer owns the vanilla map download, all normal controls are locked
// during preparation except the real song-download widget, the Ranked countdown
// is drawn on the level page, and gameplay starts automatically when the server
// enters the playing state.
bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds);

} // namespace corum::ranked
