#pragma once

class PlayLayer;

namespace corum::ranked {
// Completion overlay -> SAME canonical scene; false until exact local dummy
// authority is latched. Does not arm/reopen a Ranked Attempt or launch a level.
bool prepareSpectatorScene(PlayLayer* layer);
// Use vanilla completion-menu exit if that exact scene still owns an overlay.
// Weak ownership in EndLevelLayer.cpp prevents stale cross-scene pointers.
bool quitThroughRankedCompletionOverlay(PlayLayer* layer);
} // namespace corum::ranked
