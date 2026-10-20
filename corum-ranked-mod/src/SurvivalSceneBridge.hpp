#pragma once
class PlayLayer;
namespace corum::ranked {
bool isOfficialSurvivalPlayLayer(PlayLayer* layer);
bool requestSurvivalGameplayReturn(PlayLayer* layer);
void pumpSurvivalGameplayExit(PlayLayer* layer);
bool quitSurvivalPauseForReturn(PlayLayer* layer);
bool quitThroughSurvivalCompletionOverlay(PlayLayer* layer);
bool resumeSurvivalFreePlayPauseBeforeQuit(PlayLayer* layer);
}
