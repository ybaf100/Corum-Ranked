#pragma once

#include <Geode/Geode.hpp>

class MusicDownloadManager;

namespace corum::ranked {

// Both prepare progress and the native LevelInfo gate use GD's effective path
// (including loaded Jukebox replacements), not a guessed download filename.
bool rankedSongResourceReady(MusicDownloadManager* manager, int songId);

// Opens the real Geometry Dash LevelInfoLayer for the selected Ranked map.
// LevelInfoLayer owns the vanilla map download, all normal controls are locked
// during preparation except the real song widget and verified vanilla options
// button. The Ranked countdown
// is drawn on the level page, and gameplay starts automatically when the server
// enters the playing state.
bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds);

} // namespace corum::ranked
