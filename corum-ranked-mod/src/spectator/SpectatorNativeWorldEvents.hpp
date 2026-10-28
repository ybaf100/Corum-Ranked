#pragma once
#include "../domain/SpectatorRemotePolicy.hpp"
class PlayLayer;
class GJBaseGameLayer;
class EffectGameObject;
namespace corum::ranked {
SpectatorRandomEvents captureNativeRandomEvents(PlayLayer *);
void commitNativeRandomEvents(PlayLayer *, SpectatorRandomEvents const &);
void beginNativeWorldEventCapture(PlayLayer *);
void prepareNativeWorldEvents(PlayLayer *, SpectatorRemoteFrame const &);
void applyDueNativeRandomEvents(PlayLayer *);
void noteNativeFollowTrigger(GJBaseGameLayer *, EffectGameObject *);
void sampleNativeFollowWorld(GJBaseGameLayer *, float);
} // namespace corum::ranked
