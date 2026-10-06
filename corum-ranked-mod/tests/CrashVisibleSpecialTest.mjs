import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync, spawnSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const selections=JSON.parse(read('diagnostics/player-hook-stage3.json'));
function preprocess(s, flags=[]) {
    return execFileSync('g++',['-E','-P','-x','c++','-',...flags],{
        input:s.replace(/^\s*#include[^\n]*/gm,''),encoding:'utf8',stdio:'pipe'});
}
function method(signature) {
    const s=read('src/PlayLayer.cpp'),start=s.indexOf(signature);assert(start>=0);
    let end=s.indexOf('{',start)+1,depth=1;
    while(depth){const c=s[end++];if(c==='{')++depth;if(c==='}')--depth;}
    return s.slice(start,end);
}

test('visible/special singles and full-B complements are distinct; candidate removes both with other five intact',()=>{
    const expected={
        'stage3-visible-special-control':['setVisible','updateSpecial'],
        'stage3-player-visible-only':['setVisible'],
        'stage3-player-special-only':['updateSpecial'],
        'stage3-full-minus-visible':['update','updateRotation','spiderTestJump','setPosition','setRotation','updateSpecial'],
        'stage3-full-minus-special':['update','updateRotation','spiderTestJump','setPosition','setRotation','setVisible'],
        'stage3-full-minus-visible-special':['update','updateRotation','spiderTestJump','setPosition','setRotation'],
        'stage3-fix-candidate-debug':['update','updateRotation','spiderTestJump','setPosition','setRotation'],
        'stage3-fix-candidate-off':['update','updateRotation','spiderTestJump','setPosition','setRotation'],
    };
    for(const [name,hooks] of Object.entries(expected)){
        const v=selections.variants.find(v=>v.variant===name);assert(v,name);
        assert.deepEqual(v.enabledPlayerHooks,hooks);
        assert(v.exclusions.includes('CORUM_CRASH_DIAG_NO_PROBES_HOOKS'));
        assert.equal(v.diagnostic,'ON');
        const flags=['-DGEODE_IS_IOS=1',...v.exclusions.map(f=>`-D${f}=1`)];
        const s=preprocess(read('src/PlayLayer.cpp'),flags);
        const b=s.slice(s.indexOf('class $modify(CorumRankedSpectatorDummyPlayer,'),s.indexOf('class $modify(CorumRankedSpectatorProgressSave,'));
        const actual=[...b.matchAll(/\bvoid (\w+)\(/g)].map(m=>m[1]).filter(n=>n!=='onModify').sort();
        assert.deepEqual(actual,[...hooks].sort(),name);
        assert.equal(v.worldOwnedPresentation,name.startsWith('stage3-fix-candidate-'));
    }
    const argv=['diagnostics/verify-crash-artifact.py','--matrix','--selection','diagnostics/player-hook-stage3.json'];
    const matrix=JSON.parse(execFileSync('python3',argv,{cwd:root,encoding:'utf8'}).trim().slice(7));
    assert.equal(matrix.include.length,10);
    for(const m of matrix.include)assert.equal(m.world_owned,m.variant.startsWith('stage3-fix-candidate-')?'ON':'OFF');
});

test('candidate OFF keeps beta30 driver bodies/layout; actual CMake rejects unpaired unsafe opt-in',()=>{
    const expected={
        'src/spectator/SpectatorWorldDriver.cpp':'80d9e637f55f2c6c3366dbfc52e1ebd69506fa264707ac5064641e69f6bbad31',
        'src/spectator/SpectatorWorldDriver.hpp':'ccb74b3878021075b4a187fbb5aca1577fcf5f5efd57aa28dff6fcf05e65d02b',
    };
    for(const [path,hash] of Object.entries(expected))
        assert.equal(createHash('sha256').update(preprocess(read(path)).replace(/\s+/g,'')).digest('hex'),hash,path);
    const dir=mkdtempSync(join(tmpdir(),'corum-candidate-cmake-'));
    try{
        const sdk=join(dir,'stub-sdk');mkdirSync(sdk);
        writeFileSync(join(sdk,'CMakeLists.txt'),`function(setup_geode_mod target)
get_target_property(definitions \${target} COMPILE_DEFINITIONS)
file(WRITE \"\${CMAKE_BINARY_DIR}/definitions.txt\" \"\${definitions}\")
endfunction()
`);
        for(const [name,flags,valid] of [
            ['requires-diag',['-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=ON'],false],
            ['requires-removals',['-DCORUM_CRASH_DIAG=ON','-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=ON'],false],
            ['paired',['-DCORUM_CRASH_DIAG=ON','-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=ON',
                '-DCORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK=ON','-DCORUM_CRASH_DIAG_NO_PLAYER_UPDATE_SPECIAL_HOOK=ON'],true],
            ['forbid-physics-removal',['-DCORUM_CRASH_DIAG=ON','-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=ON',
                '-DCORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK=ON','-DCORUM_CRASH_DIAG_NO_PLAYER_UPDATE_SPECIAL_HOOK=ON',
                '-DCORUM_CRASH_DIAG_NO_PLAYER_UPDATE_HOOK=ON'],false],
            ['forbid-persistence-removal',['-DCORUM_CRASH_DIAG=ON','-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=ON',
                '-DCORUM_CRASH_DIAG_NO_PLAYER_SET_VISIBLE_HOOK=ON','-DCORUM_CRASH_DIAG_NO_PLAYER_UPDATE_SPECIAL_HOOK=ON',
                '-DCORUM_CRASH_DIAG_NO_PERSISTENCE_HOOKS=ON'],false],
            ['default',[],true],
        ]){
            const build=join(dir,name);
            const r=spawnSync('cmake',['-S',root,'-B',build,...flags],{env:{...process.env,GEODE_SDK:sdk},encoding:'utf8'});
            assert.equal(r.status===0,valid,r.stdout+r.stderr);
            if(!valid)assert.match(r.stdout+r.stderr,/World-owned candidate (requires|must retain)/);
            else if(name==='paired')assert.match(readFileSync(join(build,'definitions.txt'),'utf8'),/CORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=1/);
            else assert(!readFileSync(join(build,'definitions.txt'),'utf8').includes('CORUM_CRASH_DIAG'));
        }
    }finally{rmSync(dir,{recursive:true,force:true});}
});

test('actual candidate reconcile repairs all 200 samples and late visibility without touching an ordinary or waiting history',()=>{
    const dir=mkdtempSync(join(tmpdir(),'corum-world-owned-'));
    try{
        mkdirSync(join(dir,'Geode/binding'),{recursive:true});
        writeFileSync(join(dir,'Geode/Geode.hpp'),read('tests/helpers/SpectatorNativeDouble.hpp'));
        for(const name of ['PlayLayer','GJGameLevel','PlayerObject'])writeFileSync(join(dir,`Geode/binding/${name}.hpp`),'#include <Geode/Geode.hpp>\n');
        const cpp=`
#include "${join(root,'src/spectator/SpectatorWorldDriver.hpp')}"
#include <Geode/Geode.hpp>
#include <cassert>
using namespace corum::ranked;
struct Owner:PlayLayer{
 struct Fields{SpectatorWorldDriver spectatorWorldDriver;} fields;Fields* m_fields=&fields;
 bool active=false;bool spectatorCameraActive(){return active;}
 ${method('void reconcileSpectatorWorldDriver()')}
};
int main(){
 Owner scene;PlayerObject local,other;GJGameLevel level;
 scene.m_player1=&local;scene.m_player2=&other;scene.m_level=&level;local.m_gameLayer=&scene;
 local.setVisible(true);local.updateSpecial(.027f);auto history=local.m_playerFollowFloats;auto index=local.m_followRelated;
 scene.reconcileSpectatorWorldDriver();assert(local.isVisible());assert(local.m_playerFollowFloats==history&&local.m_followRelated==index);
 scene.active=true;scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());assert(local.m_playerFollowFloats==history&&local.m_followRelated==index); // waiting: hide only
 SpectatorRemoteContext c{"match",1,0,level.m_levelID,"attempt",1,1};
 auto f=SpectatorRemoteFrame{c,0,SpectatorGhostPose{100,40,0},0,0};auto& driver=scene.fields.spectatorWorldDriver;
 assert(driver.apply(&scene,true,f));f.sequence=1;f.publisherElapsedMs=50;f.pose->y=140;assert(driver.apply(&scene,true,f));
 assert(local.m_followRelated==5);for(int i=1;i<=5;++i)assert(local.m_playerFollowFloats[i]==40+i*20);
 auto authoritative=local.m_playerFollowFloats;auto otherHistory=other.m_playerFollowFloats;
 local.setVisible(true);local.setPosition({9999,9999});local.updateSpecial(.027f);
 scene.reconcileSpectatorWorldDriver();assert(!local.isVisible());assert(local.getPosition().x==100&&local.getPosition().y==140);
 assert(local.m_followRelated==5&&local.m_unk838==0&&local.m_playerFollowFloats==authoritative);
 assert(other.isVisible()&&other.m_playerFollowFloats==otherHistory);
 local.m_playerFollowFloats.resize(1);local.m_followRelated=777;local.m_unk838=.06f;
 scene.reconcileSpectatorWorldDriver();assert(local.m_playerFollowFloats==authoritative&&local.m_followRelated==5&&local.m_unk838==0);
 f.presentationSegment=1;f.sequence=2;f.publisherElapsedMs=100;f.pose->y=740;assert(driver.apply(&scene,true,f));
 assert(local.m_followRelated==0);for(float y:local.m_playerFollowFloats)assert(y==740);
 driver.reset();local.setVisible(true);scene.active=false;scene.reconcileSpectatorWorldDriver();assert(local.isVisible());
 assert(local.physicsUpdates==0&&local.rotationUpdates==0&&local.spiderJumps==0);
}
`;
        const file=join(dir,'test.cpp'),binary=join(dir,'test');writeFileSync(file,cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',
            '-DCORUM_CRASH_DIAG_WORLD_OWNED_PRESENTATION=1','-I',dir,
            join(root,'src/spectator/SpectatorWorldDriver.cpp'),file,'-o',binary],{stdio:'pipe'});
        execFileSync(binary,[],{stdio:'pipe'});
    }finally{rmSync(dir,{recursive:true,force:true});}
});

test('artifact verification distinguishes full safety candidate from hook-only isolation and rejects a missing candidate path',()=>{
    const script=join(root,'diagnostics/verify-crash-artifact.py');
    execFileSync('python3',['-c',`
import sys,importlib.util,json,struct
sys.dont_write_bytecode=True
spec=importlib.util.spec_from_file_location('verify',${JSON.stringify(script)});m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
m.SELECTION=json.loads(open(${JSON.stringify(join(root,'diagnostics/player-hook-stage3.json'))}).read())
for v in m.SELECTION['variants']:
 targets=m.expected_targets(v)
 b=struct.pack('<II',0xFEEDFACF,0x0100000C)+b''.join(('[GEODE_MODIFY_NAME] '+t+' [GEODE_MODIFY_ADDRESS] 0x0\\0').encode() for t in targets)
 if v['diagnostic']=='ON':b+=b'[CorumCrashDiag]'
 if v.get('worldOwnedPresentation',False):b+=b'world-owned-presentation=true'
 assert set(m.verify_binary(b,v))==targets
 if v.get('worldOwnedPresentation',False):
  assert 'PlayerObject::setVisible' not in targets and 'PlayerObject::updateSpecial' not in targets
  assert {'PlayerObject::update','PlayerObject::updateRotation','PlayerObject::spiderTestJump','PlayerObject::setPosition','PlayerObject::setRotation','GJGameLevel::storeNewLocalScore','PlayLayer::levelComplete'}<=targets
  try:m.verify_binary(b.replace(b'world-owned-presentation=true',b''),v)
  except AssertionError:pass
  else:raise AssertionError('plain detour removal accepted as full candidate')
`],{stdio:'pipe'});
});
