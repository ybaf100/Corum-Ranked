#pragma once

class PlayLayer;

namespace corum::ranked {
// Completion overlay -> SAME canonical scene; false until exact local dummy
// authority is latched. Does not arm/reopen a Ranked Attempt or launch a level.
bool prepareSpectatorScene(PlayLayer* layer);
// Use vanilla completion-menu exit if that exact scene still owns an overlay.
// Weak ownership in EndLevelLayer.cpp prevents stale cross-scene pointers.
bool quitThroughRankedCompletionOverlay(PlayLayer* layer);
// Idempotent, deferred scene return after an authenticated concession. It does
// not use a timer deadline or create an additional Attempt End.
bool requestConcededGameplayReturn(PlayLayer* layer);
// Enter the exact owned Pause's native Quit after authenticated concession.
// True means the native route was invoked; it never grants manual exit.
bool quitRankedPauseForConcession(PlayLayer* layer);
// Retire the exact scene's native Ranked Pause before quitting gameplay.
// False means vanilla Resume did not restore an unpaused scene; never force flags.
bool resumeRankedPauseBeforeQuit(PlayLayer* layer);
} // namespace corum::ranked
