import test from 'node:test';
import {writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
test('production initial physics snapshot restores Cube/Ship motion, holds, gravity, dash and slope fields once, and refuses incomplete or mismatched initial worlds',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;
 if(!json||!result)throw Error('Pinned Geode JSON dependencies required');
 const dir=mkdtempSync(join(tmpdir(),'replay-player-state-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "replay/SurvivalReplayPlayerState.hpp"
#include <cassert>
#include <map>
namespace cocos2d{struct CCPoint{float x=0,y=0;};}
struct Player{
#define DECLARE_FIELD(type,name) type name{};
 CORUM_REPLAY_PHYSICS_SCALARS(DECLARE_FIELD)
 CORUM_REPLAY_PHYSICS_PLATFORM(DECLARE_FIELD)
 CORUM_REPLAY_PHYSICS_POINTS(DECLARE_FIELD)
#undef DECLARE_FIELD
 std::map<int,bool>m_holdingButtons;cocos2d::CCPoint position;float rotation=0,sx=1,sy=1;int positionWrites=0;
 float getPositionX()const{return position.x;}float getPositionY()const{return position.y;}float getRotation()const{return rotation;}float getScaleX()const{return sx;}float getScaleY()const{return sy;}
 void setPosition(cocos2d::CCPoint p){position=p;++positionWrites;}void setRotation(float v){rotation=v;}void setScaleX(float v){sx=v;}void setScaleY(float v){sy=v;}
};
using namespace corum::ranked::replay;
int main(){
 auto refs=PhysicsJson::object();refs["floor"]=17;
 for(bool ship:{false,true}){
  Player source,target;source.m_isShip=target.m_isShip=ship;source.position={15,108.642f};source.rotation=2.703f;
  source.m_yVelocity=15.125;source.m_yVelocityBeforeSlope=3.5;source.m_gravity=-.9;source.m_gravityMod=.75f;source.m_shipRotation={-1.25f,3.75f};
  source.m_jumpBuffered=true;source.m_wasJumpBuffered=true;source.m_holdingButtons={{0,false},{1,true},{2,false},{3,true}};
  source.m_dashX=1.5;source.m_dashY=-2.5;source.m_dashAngle=.125;source.m_dashStartTime=12.5;source.m_isDashing=true;
  source.m_isOnSlope=true;source.m_slopeVelocity=2.25f;source.m_slopeStartTime=2.75;source.m_stateForceVector={.5f,2.5f};source.m_stateForce=2;
  auto state=captureReplayPlayerState(&source,refs);assert(replayPlayerStateProblem(&target,state,refs).empty());
  applyReplayPlayerState(&target,state);assert(target.positionWrites==1);assert(captureReplayPlayerState(&target,refs)==state);
  auto before=target.m_yVelocity;auto damaged=state;damaged.erase("m_yVelocity");assert(!replayPlayerStateProblem(&target,damaged,refs).empty());assert(target.m_yVelocity==before&&target.positionWrites==1);
  auto other=refs;other["floor"]=18;assert(replayPlayerStateProblem(&target,state,other)=="INITIAL_WORLD_REFERENCE_MISMATCH");
  damaged=state;damaged["m_isBird"]=true;assert(replayPlayerStateProblem(&target,damaged,refs)=="INITIAL_MODE_MISMATCH");
  damaged=state;damaged["holdingButtons"]=PhysicsJson::array();auto pair=PhysicsJson::array();pair.push(1);pair.push(true);damaged["holdingButtons"].push(pair);damaged["holdingButtons"].push(pair);assert(replayPlayerStateProblem(&target,damaged,refs)=="INITIAL_HOLDS_INVALID");
  assert(replayPlayerStateProblem(&target,PhysicsJson::object(),refs)=="INITIAL_PHYSICS_UNAVAILABLE");
 }
 Player* missing=nullptr;assert(captureReplayPlayerState(missing,refs).isNull());assert(replayPlayerStateProblem(missing,nullptr,refs).empty());
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-isystem',json+'/include','-isystem',result,'-I',join(root,'src'),join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
