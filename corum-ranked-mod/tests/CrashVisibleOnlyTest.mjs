import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync, spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const selection=JSON.parse(read('diagnostics/player-hook-stage4.json'));
const flags=['-DCORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY=1','-DCORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK=1'];
function preprocess(s,defines=[]) {
    return execFileSync('g++',['-E','-P','-x','c++','-',...defines],{
        input:s.replace(/^\s*#include[^\n]*/gm,''),encoding:'utf8',stdio:'pipe'});
}
function method(s,signature) {
    const start=s.indexOf(signature);assert(start>=0,signature);
    let end=s.indexOf('{',start)+1,depth=1;
    while(depth){const c=s[end++];if(c==='{')++depth;if(c==='}')--depth;}
    return s.slice(start,end);
}

test('visible-only fix excludes the actual setter and priority; retains identical updateSpecial and all six other B hooks',()=>{
    const source=read('src/PlayLayer.cpp');
    const expected=['update','updateRotation','spiderTestJump','setPosition','setRotation','updateSpecial'];
    for(const v of selection.variants){
        const defines=['-DGEODE_IS_IOS=1',...v.exclusions.map(f=>`-D${f}=1`),
            ...(v.worldOwnedVisibility?['-DCORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY=1']:[])];
        const s=preprocess(source,defines);
        const b=method(s,'class $modify(CorumRankedSpectatorDummyPlayer,');
        assert.deepEqual([...b.matchAll(/\bvoid (\w+)\(/g)].map(m=>m[1]).filter(n=>n!=='onModify').sort(),[...v.enabledPlayerHooks].sort());
        if(v.worldOwnedVisibility){
            assert.deepEqual(v.enabledPlayerHooks,expected);
            assert(!b.includes('"PlayerObject::setVisible"'));
            assert.match(b,/"PlayerObject::updateSpecial"/);
            assert.equal(method(b,'void updateSpecial(float dt)'),method(preprocess(source,['-DGEODE_IS_IOS=1']),'void updateSpecial(float dt)'));
            assert(!v.worldOwnedPresentation);
            assert(v.exclusions.includes('CORUM_CRASH_DIAG_NO_PROBES_HOOKS'));
            const post=method(s,'void postUpdate(float dt)');
            assert(post.indexOf('reconcileSpectatorWorldDriver();')>post.indexOf('PlayLayer::postUpdate(dt);'));
        }
    }
    for(const path of ['src/spectator/SpectatorWorldDriver.cpp','src/spectator/SpectatorWorldDriver.hpp'])
        assert.equal(preprocess(read(path),flags),preprocess(read(path)),`${path} has no Follow-Y replacement`);
    const matrix=JSON.parse(execFileSync('python3',['diagnostics/verify-crash-artifact.py','--matrix',
        '--selection','diagnostics/player-hook-stage4.json'],{cwd:root,encoding:'utf8'}).trim().slice(7));
    assert.equal(matrix.include.length,5);
    for(const m of matrix.include){
        assert.equal(m.world_owned,'OFF');
        assert.equal(m.visibility_owned,m.variant.startsWith('stage4-visible-fix-')?'ON':'OFF');
    }
    const workflow=read('../.github/workflows/corum-crash-diagnostic.yml');
    assert(workflow.includes('-DCORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY=${{ matrix.visibility_owned }}'));
    assert(workflow.includes('--selection corum-ranked-mod/diagnostics/player-hook-stage4.json'));
});

test('actual CMake rejects visible-only opt-in that removes Follow-Y, physics, result or persistence protection',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-visible-cmake-'));
    const good=['-DCORUM_CRASH_DIAG=ON','-DCORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY=ON','-DCORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK=ON'];
    try{
        const sdk=join(dir,'sdk');mkdirSync(sdk);
        writeFileSync(join(sdk,'CMakeLists.txt'),'function(setup_geode_mod target)\nget_target_property(definitions ${target} COMPILE_DEFINITIONS)\nfile(WRITE "${CMAKE_BINARY_DIR}/definitions.txt" "${definitions}")\nendfunction()\n');
        const cases=[['valid',good,true],['no-diag',good.filter(f=>!f.startsWith('-DCORUM_CRASH_DIAG=')),false],
            ['setter-retained',good.filter(f=>!f.includes('NO_PLAYER_SET_VISIBLE')),false],
            ...['NO_PLAYER_UPDATE_SPECIAL_HOOK','NO_PLAYER_UPDATE_HOOK','NO_WORLD_HOOKS','NO_PERSISTENCE_HOOKS',
                'NO_GAMEPLAY_HOOKS','WORLD_OWNED_PRESENTATION'].map(f=>[f,[...good,`-DCORUM_CRASH_DIAG_${f}=ON`],false])];
        for(const [name,defines,valid] of cases){
            const build=join(dir,name),r=spawnSync('cmake',['-S',root,'-B',build,...defines],{env:{...process.env,GEODE_SDK:sdk},encoding:'utf8'});
            assert.equal(r.status===0,valid,r.stdout+r.stderr);
            if(valid){const d=readFileSync(join(build,'definitions.txt'),'utf8');assert(d.includes('CORUM_CRASH_DIAG_WORLD_OWNED_VISIBILITY=1'));assert(!d.includes('CORUM_CRASH_DIAG_NO_PLAYER_UPDATE_SPECIAL_HOOK'));}
        }
    }finally{rmSync(dir,{recursive:true,force:true});}
});

test('actual visibility owner hides authorized and waiting dummy only; normal Practice and Follow-Y history are unchanged',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-visible-owner-'));
    try{
        mkdirSync(join(dir,'Geode/binding'),{recursive:true});
        writeFileSync(join(dir,'Geode/Geode.hpp'),read('tests/helpers/SpectatorNativeDouble.hpp'));
        for(const name of ['PlayLayer','GJGameLevel','PlayerObject'])writeFileSync(join(dir,`Geode/binding/${name}.hpp`),'#include <Geode/Geode.hpp>\n');
        const source=read('src/PlayLayer.cpp');
        const cpp=`
#include "${join(root,'src/spectator/SpectatorWorldDriver.hpp')}"
#include <Geode/Geode.hpp>
#include <cassert>
namespace corum::ranked {struct RankedRuntime{bool spectator=false;static RankedRuntime& get(){static RankedRuntime r;return r;}bool isSpectating(){return spectator;}};}
struct Owner:PlayLayer{
 struct Fields{bool spectatorScenePrepared=false,spectatorPresentationRetired=false,autoExitRequested=false;struct Safety{bool enabled=false;bool active(){return enabled;}}localSafety;corum::ranked::SpectatorWorldDriver spectatorWorldDriver;} fields;
 Fields* m_fields=&fields;bool watchable=true;bool watchableSpectatorScene(){return watchable;}
 ${method(source,'bool spectatorCameraActive()')}
 ${method(source,'void reconcileSpectatorWorldDriver()')}
};
int main(){
 using namespace corum::ranked;Owner scene;PlayerObject local,second;GJGameLevel level;
 scene.m_player1=&local;scene.m_player2=&second;scene.m_level=&level;local.m_gameLayer=&scene;
 auto history=local.m_playerFollowFloats;
 scene.m_isPracticeMode=true;local.setVisible(true);scene.reconcileSpectatorWorldDriver();assert(local.isVisible());assert(local.m_playerFollowFloats==history);
 auto& runtime=RankedRuntime::get();scene.fields.spectatorScenePrepared=true;scene.fields.localSafety.enabled=true;runtime.spectator=true;
 scene.reconcileSpectatorWorldDriver();assert(local.isVisible()); // Practice guard wins, even with stale spectator state
 scene.m_isPracticeMode=false;scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());assert(local.m_playerFollowFloats==history); // no remote frame yet
 SpectatorRemoteContext context{"match",1,0,level.m_levelID,"attempt",1,1};auto frame=SpectatorRemoteFrame{context,0,SpectatorGhostPose{100,40,0},0,0};
 auto& driver=scene.fields.spectatorWorldDriver;assert(driver.apply(&scene,true,frame));frame.sequence=1;frame.publisherElapsedMs=50;frame.pose->y=140;assert(driver.apply(&scene,true,frame));
 assert(local.m_followRelated==5);for(int i=1;i<=5;++i)assert(local.m_playerFollowFloats[i]==40+i*20);
 auto ring=local.m_playerFollowFloats;auto index=local.m_followRelated;auto accumulator=local.m_unk838;
 local.setVisible(true);scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());
 assert(local.m_playerFollowFloats==ring&&local.m_followRelated==index&&local.m_unk838==accumulator);
 assert(second.isVisible());
 scene.fields.spectatorPresentationRetired=true;local.setVisible(true);scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 assert(local.physicsUpdates==0&&local.rotationUpdates==0&&local.spiderJumps==0);
}
`;
        const file=join(dir,'test.cpp'),binary=join(dir,'test');writeFileSync(file,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',...flags,'-I',dir,
            join(root,'src/spectator/SpectatorWorldDriver.cpp'),file,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    }finally{rmSync(dir,{recursive:true,force:true});}
});

test('packaged marker verifier requires visibility replacement and original Follow-Y detour, with no new native targets',()=>{
    execFileSync('python3',['-c',`
import sys,json,struct,importlib.util
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('v',${JSON.stringify(join(root,'diagnostics/verify-crash-artifact.py'))});m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.SELECTION=json.loads(open(${JSON.stringify(join(root,'diagnostics/player-hook-stage4.json'))}).read())
for v in m.SELECTION['variants']:
 targets=m.expected_targets(v)
 binary=struct.pack('<II',0xFEEDFACF,0x0100000C)+b''.join(('[GEODE_MODIFY_NAME] '+t+' [GEODE_MODIFY_ADDRESS] 0x0\\0').encode() for t in targets)
 if v['diagnostic']=='ON':binary+=b'[CorumCrashDiag]'
 if v.get('worldOwnedVisibility'):binary+=b'world-owned-visibility=true'
 assert set(m.verify_binary(binary,v))==targets
 if v.get('worldOwnedVisibility'):
  assert 'PlayerObject::setVisible' not in targets and 'PlayerObject::updateSpecial' in targets
  assert {'PlayLayer::levelComplete','GJGameLevel::storeNewLocalScore'}<=targets
  for bad in [binary.replace(b'world-owned-visibility=true',b''),binary.replace(b'[GEODE_MODIFY_NAME] PlayerObject::updateSpecial ',b'[IMPORTED_NAME] PlayerObject::updateSpecial ')]:
   try:m.verify_binary(bad,v)
   except AssertionError:pass
   else:raise AssertionError('unsafe/mislabelled visible-only package accepted')
`],{stdio:'pipe'});
});
