import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
const source = read('src/PlayLayer.cpp');
function block(text, signature) {
    const start = text.indexOf(signature);
    assert(start >= 0, signature);
    let end = text.indexOf('{', start) + 1, depth = 1;
    while (depth) {
        const char = text[end++];
        if (char === '{') ++depth;
        if (char === '}') --depth;
        assert(end <= text.length, signature);
    }
    return text.slice(start, end);
}

test('production PlayerObject hooks exclude the visibility detour and priority on every target', () => {
    for (const platform of ['IOS', 'WINDOWS', 'ANDROID']) {
        for (const debug of [false, true]) {
            const flags = [`-DGEODE_IS_${platform}=1`, ...(debug ? ['-DCORUM_RANKED_DEBUG_SPECTATE=1'] : [])];
            const processed = execFileSync('g++', ['-E', '-P', '-x', 'c++', '-', ...flags], {
                input: source.replace(/^\s*#include[^\n]*/gm, ''), encoding: 'utf8', stdio: 'pipe',
            });
            const hooks = block(processed, 'class $modify(CorumRankedSpectatorDummyPlayer,');
            const expected = ['update', 'updateRotation', 'spiderTestJump', 'setPosition', 'setRotation'];
            if (platform !== 'WINDOWS') expected.push('updateSpecial');
            assert.deepEqual([...hooks.matchAll(/\bvoid (\w+)\(/g)].map(match => match[1])
                .filter(name => name !== 'onModify').sort(), expected.sort());
            assert(!processed.includes('"PlayerObject::setVisible"'));
            const postUpdate = block(processed, 'void postUpdate(float dt)');
            assert(postUpdate.indexOf('reconcileSpectatorWorldDriver();') > postUpdate.indexOf('PlayLayer::postUpdate(dt);'));
        }
    }
});

test('native visibility owner hides only an authorized dummy, including waiting; preserves Practice and Follow-Y', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-visibility-owner-'));
    try {
        mkdirSync(join(dir, 'Geode/binding'), { recursive: true });
        writeFileSync(join(dir, 'Geode/Geode.hpp'), read('tests/helpers/SpectatorNativeDouble.hpp'));
        for (const name of ['PlayLayer', 'GJGameLevel', 'PlayerObject'])
            writeFileSync(join(dir, `Geode/binding/${name}.hpp`), '#include <Geode/Geode.hpp>\n');
        const cpp = `
#include "${join(root, 'src/spectator/SpectatorWorldDriver.hpp')}"
#include "${join(root,'tests/helpers/InactiveReplayDouble.hpp')}"
#include <Geode/Geode.hpp>
#include <cassert>
namespace corum::ranked {struct SurvivalSpectatorRuntime {static SurvivalSpectatorRuntime& get(){static SurvivalSpectatorRuntime r;return r;}bool sceneLevel(int)const{return false;}replay::InactiveWorld* world()const{return nullptr;}bool publishing()const{return false;}bool canLaunch()const{return false;}template<class...T>bool arm(T...){return false;}template<class...T>bool capture(T const&...){return false;}template<class...T>void terminal(T const&...){}void sceneExited(){}bool exitRequested()const{return false;}};}

namespace corum::ranked {struct RankedRuntime{struct Match{bool spectatorLiveEnabled=true;};struct View{Match match;} data;View const& view()const{return data;}bool spectator=false;static RankedRuntime& get(){static RankedRuntime r;return r;}bool isSpectating(){return spectator;}};}
struct Owner:PlayLayer {
 struct Fields{int levelId=123;bool survivalLiveLevel=false,survivalReplayLevel=false;bool spectatorScenePrepared=false,spectatorPresentationRetired=false,autoExitRequested=false;struct Safety{bool enabled=false;bool active(){return enabled;}}localSafety;corum::ranked::SpectatorWorldDriver spectatorWorldDriver;} fields;
 Fields* m_fields=&fields;bool watchable=true;bool watchableSpectatorScene(){return watchable;}
 ${block(source, 'bool spectatorCameraActive()')}
 ${block(source, 'void reconcileSpectatorWorldDriver()')}
};
int main(){
 using namespace corum::ranked;Owner scene;PlayerObject local,second;GJGameLevel level;
 scene.m_player1=&local;scene.m_player2=&second;scene.m_level=&level;local.m_gameLayer=&scene;
 auto history=local.m_playerFollowFloats;
 scene.m_isPracticeMode=true;local.setVisible(true);scene.reconcileSpectatorWorldDriver();assert(local.isVisible());assert(local.m_playerFollowFloats==history);
 auto& runtime=RankedRuntime::get();scene.fields.spectatorScenePrepared=true;scene.fields.localSafety.enabled=true;runtime.spectator=true;
 scene.reconcileSpectatorWorldDriver();assert(local.isVisible()); // stale spectator state cannot own Practice
 scene.m_isPracticeMode=false;scene.m_isTestMode=true;scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 scene.m_isTestMode=false;runtime.spectator=false;scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 runtime.spectator=true;scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());assert(local.m_playerFollowFloats==history); // waiting for first pose
 SpectatorRemoteContext context{"match",1,0,level.m_levelID,"attempt",1,1};auto frame=SpectatorRemoteFrame{context,0,SpectatorGhostPose{100,40,0},0,0};
 auto& driver=scene.fields.spectatorWorldDriver;assert(driver.apply(&scene,true,frame));frame.sequence=1;frame.publisherElapsedMs=50;frame.pose->y=140;assert(driver.apply(&scene,true,frame));
 assert(local.m_followRelated==5);for(int i=1;i<=5;++i)assert(local.m_playerFollowFloats[i]==40+i*20);
 auto ring=local.m_playerFollowFloats;auto index=local.m_followRelated;auto accumulator=local.m_unk838;
 local.setVisible(true);assert(local.isVisible());scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());
 assert(local.m_playerFollowFloats==ring&&local.m_followRelated==index&&local.m_unk838==accumulator);
 assert(!second.isVisible()); // actual native P2 is hidden; only the independent renderer may show a Dual avatar
 scene.fields.spectatorPresentationRetired=true;local.setVisible(true);scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 scene.fields.spectatorPresentationRetired=false;scene.fields.autoExitRequested=true;scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 assert(local.physicsUpdates==0&&local.rotationUpdates==0&&local.spiderJumps==0);
}
`;
        const file = join(dir, 'test.cpp'), binary = join(dir, 'test');
        writeFileSync(file, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-I', dir,
            join(root, 'src/spectator/SpectatorWorldDriver.cpp'), file, '-o', binary], { stdio: 'pipe' });
        execFileSync(binary, [], { stdio: 'pipe' });
    } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('packaged native verifier rejects restored visibility hooks, missing safety and Debug OFF probes', () => {
    execFileSync('python3', ['-c', `
import sys,struct,importlib.util
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('v',${JSON.stringify(join(root, 'tests/verify-visibility-package.py'))});v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
player={'PlayerObject::'+name for name in ('update','updateRotation','spiderTestJump','setPosition','setRotation','updateSpecial')}
safety={'GJGameLevel::savePercentage','PlayLayer::destroyPlayer','PlayLayer::levelComplete','PlayLayer::checkForEnd','PlayLayer::showCompleteText','PlayLayer::showNewBest','PlayLayer::onQuit','PlayLayer::onExit','PlayLayer::resetLevel','PlayLayer::updateAttempts','PauseLayer::customSetup','GameManager::returnToLastScene','GJBaseGameLayer::update','GJBaseGameLayer::updateCamera','GJBaseGameLayer::teleportPlayer'}
safety|={'RandTriggerGameObject::triggerObject','GJBaseGameLayer::spawnGroup','GJBaseGameLayer::checkCollisions','GJBaseGameLayer::collisionCheckObjects','PlayLayer::startMusic','GJBaseGameLayer::processCommands','GJBaseGameLayer::processQueuedButtons','GJBaseGameLayer::handleButton'}
probes={'GJBaseGameLayer::playerTouchedTrigger','EffectGameObject::triggerObject'}
def binary(targets):return struct.pack('<II',0xFEEDFACF,0x0100000C)+b''.join(('[GEODE_MODIFY_NAME] '+name+' [GEODE_MODIFY_ADDRESS] 0x0\\0').encode() for name in targets)
assert set(v.verify_binary(binary(player|safety),'ios',False))==player
assert set(v.verify_binary(binary(player|safety|probes),'ios',True))==player
for missing in ['RandTriggerGameObject::triggerObject','GJBaseGameLayer::spawnGroup','GJBaseGameLayer::checkCollisions','GJBaseGameLayer::collisionCheckObjects']:
 for debug in [False,True]:
  try:v.verify_binary(binary((player|safety|(probes if debug else set()))-{missing}),'ios',debug)
  except ValueError:pass
  else:raise AssertionError('missing production trigger boundary accepted')
for data in [binary(player|safety|{'PlayerObject::setVisible'}),binary(player|safety|{'GJGameLevel::storeNewLocalScore'}),binary(player|safety|{'GJGameLevel::saveNewScore'}),binary((player|safety)-{'PlayerObject::updateSpecial'}),binary((player|safety)-{'GJGameLevel::savePercentage'}),binary((player|safety)-{'PlayLayer::levelComplete'}),binary((player|safety)-{'PauseLayer::customSetup'}),binary((player|safety)-{'GameManager::returnToLastScene'}),binary(player|safety|probes),binary(player|safety)+b'[CorumCrashDiag]']:
 try:v.verify_binary(data,'ios',False)
 except ValueError:pass
 else:raise AssertionError('unsafe or mislabelled native package accepted')
`], { stdio: 'pipe' });
});
