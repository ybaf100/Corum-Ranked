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
// True means its native route reached the PlayLayer quit boundary. False leaves
// the existing deferred fallback available; it never grants manual exit.
bool quitRankedPauseForConcession(PlayLayer* layer);
// Retire the exact scene's native Ranked Pause before quitting gameplay.
// Authenticated native Pause Quit owns its teardown even with intermediate pause
// flags. Independent quit requires both native pause states restored by Resume.
bool resumeRankedPauseBeforeQuit(PlayLayer* layer);
} // namespace corum::ranked
