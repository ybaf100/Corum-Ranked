#pragma once

class GJGameLevel;

namespace corum::ranked {

// Opens the real Geometry Dash LevelInfoLayer for the selected Ranked map and
// turns it into a song-download-only gate. The actual vanilla CustomSongWidget
// remains interactive; the rest of LevelInfoLayer is masked/blocked.
bool showRankedSongDownloadGate(GJGameLevel* level, double countdownRemainingSeconds);

} // namespace corum::ranked
