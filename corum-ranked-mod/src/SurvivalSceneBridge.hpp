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

namespace corum::ranked {void presentSurvivalReplayCamera(PlayLayer*);}

namespace corum::ranked {void finishSurvivalReplayForReturn(PlayLayer*);}

class PlayerObject;
namespace corum::ranked {void survivalReplayNativeSnap(PlayerObject*,bool,float,float);}

namespace corum::ranked {void advanceSurvivalReplayWorld(PlayLayer*,float);}
namespace corum::ranked {void survivalReplayPlayerStep(PlayerObject*,float,bool);}
