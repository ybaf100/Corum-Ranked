import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const instrumentation = readFileSync(join(root, 'src/spectator/SpectatorNativeInstrumentation.cpp'), 'utf8');

test('actual native probes delegate once, preserve activation decisions and observe only the scoped scene/player', () => {
    const production = instrumentation.replace(/^#include.*$|^using namespace geode::prelude;$/gm, '')
        .replace('class $modify(CorumSpectatorNativeInteractionProbe, GJBaseGameLayer)', 'struct InteractionProbe : GJBaseGameLayer')
        .replace('class $modify(CorumSpectatorNativeTriggerProbe, EffectGameObject)', 'struct TriggerProbe : EffectGameObject');
    const cpp = `
#include "${join(root, 'src/domain/SpectatorNativeProbe.hpp')}"
#include <cassert>
#include <vector>
namespace gd {template<class T>using vector=std::vector<T>;}
struct PlayerObject{};struct GameObject{int m_uniqueID=42;};struct TeleportPortalObject{};
struct EffectGameObject;
struct GJBaseGameLayer {
int calls=0;bool allowed=false;
int checkCollisions(PlayerObject*,float,bool){++calls;return 17;}
void collisionCheckObjects(PlayerObject*,gd::vector<GameObject*>*,int,float){++calls;}
void playerTouchedObject(PlayerObject*,GameObject*){++calls;}
void playerTouchedTrigger(PlayerObject*,EffectGameObject*){++calls;}
bool canBeActivatedByPlayer(PlayerObject*,EffectGameObject*){++calls;return allowed;}
void teleportPlayer(TeleportPortalObject*,PlayerObject*){++calls;}
void checkSpawnObjects(){++calls;}
void processCommands(float,bool,bool){++calls;}
void processPlayerFollowActions(float){++calls;}
void processDynamicObjectActions(int,float){++calls;}
};
namespace corum::ranked {void sampleNativeFollowWorld(GJBaseGameLayer*,float){}void noteNativeFollowTrigger(GJBaseGameLayer*,EffectGameObject*){}}
struct EffectGameObject:GameObject {
int calls=0;virtual void triggerObject(GJBaseGameLayer*,int,gd::vector<int>const*){++calls;}
};
${production}
int main(){
#if defined(CORUM_RANKED_DEBUG_SPECTATE)
using namespace corum::ranked;SpectatorNativeProbe counts;InteractionProbe layer,otherLayer;
PlayerObject p,other;TriggerProbe trigger;
{
ScopedSpectatorNativeProbe scope(counts,&layer,&p,true);
assert(layer.checkCollisions(&p,.01f,true)==17);layer.collisionCheckObjects(&p,nullptr,0,.01f);
layer.playerTouchedTrigger(&p,&trigger);assert(!layer.canBeActivatedByPlayer(&p,&trigger));
layer.allowed=true;assert(layer.canBeActivatedByPlayer(&p,&trigger));
layer.teleportPlayer(nullptr,&p);layer.checkSpawnObjects();layer.processCommands(.01f,false,true);
layer.processPlayerFollowActions(.01f);layer.processDynamicObjectActions(0,.01f);
trigger.triggerObject(&layer,0,nullptr);trigger.triggerObject(&layer,0,nullptr);
assert(layer.calls==10&&trigger.calls==2);assert(counts.count(SpectatorNativePath::CheckCollisions)==1);
assert(counts.count(SpectatorNativePath::ActivationQuery)==2&&counts.count(SpectatorNativePath::ActivationAllowed)==1);
assert(counts.count(SpectatorNativePath::TriggerObject)==2&&counts.repeatedTriggerCalls==1);
otherLayer.checkCollisions(&p,.01f,false);layer.checkCollisions(&other,.01f,false);
assert(counts.count(SpectatorNativePath::CheckCollisions)==1);
}
layer.checkCollisions(&p,.01f,false);assert(counts.count(SpectatorNativePath::CheckCollisions)==1);
assert(!activeSpectatorNativeProbe&&!spectatorNativeProbeLayer&&!spectatorNativeProbePlayer);
counts.clearWindow();assert(counts.frames==0&&counts.repeatedTriggerCalls==0);
{ScopedSpectatorNativeProbe scope(counts,&layer,&p,false);layer.checkSpawnObjects();}
assert(counts.count(SpectatorNativePath::Spawn)==0);
#endif
}
`;
    const dir = mkdtempSync(join(tmpdir(), 'corum-native-probe-'));
    try {
        const file = join(dir, 'probe.cpp'), binary = join(dir, 'probe'); writeFileSync(file, cpp);
        for (const flags of [[], ['-DCORUM_RANKED_DEBUG_SPECTATE=1'],
            ['-DCORUM_RANKED_DEBUG_SPECTATE=1', '-DGEODE_IS_WINDOWS=1'],
            ['-DCORUM_RANKED_DEBUG_SPECTATE=1', '-DGEODE_IS_IOS=1']]) {
            execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Werror', ...flags, file, '-o', binary], { stdio: 'pipe' });
            execFileSync(binary, [], { stdio: 'pipe' });
        }
    } finally { rmSync(dir, { recursive: true, force: true }); }
});
