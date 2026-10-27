import test from 'node:test';import assert from 'node:assert/strict';import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';import {execFileSync} from 'node:child_process';
const root=resolve(new URL('..',import.meta.url).pathname),native=readFileSync(join(root,'src/replay/SurvivalReplayNative.cpp'),'utf8'),play=readFileSync(join(root,'src/PlayLayer.cpp'),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0);let p=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[p]==='{')-(source[p]==='}');++p;}return source.slice(start,p);}
test('production native replay boundary records only consumed P1/P2 commands, blocks viewer input and advances one exact no-save native owner',()=>{
 const dir=mkdtempSync(join(tmpdir(),'replay-native-boundary-'));try{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;assert(json&&result);
 const cpp=`
#include <matjson.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include "${root}/src/spectator/ScopedSpectatorPersistenceMode.hpp"
namespace gd{template<class T>using vector=std::vector<T>;}
enum class PlayerButton{Jump=1};struct PlayerButtonCommand{PlayerButton m_button=PlayerButton::Jump;bool m_isPush=false,m_isPlayer2=false;int m_step=0;double m_timestamp=0;};
struct GJGameLevel{int m_levelID=123;};
struct GJBaseGameLayer{gd::vector<PlayerButtonCommand>m_queuedButtons;int m_currentStep=0,nativeInputs=0,nativeUpdates=0,consumed=0;bool m_isTestMode=false,m_isPracticeMode=false,m_isPaused=false;void handleButton(bool,int,bool){++nativeInputs;}
 void processQueuedButtons(float,bool){std::erase_if(m_queuedButtons,[&](auto const&c){if(c.m_step<=m_currentStep){++consumed;return true;}return false;});}
 void update(float){++nativeUpdates;assert(m_isTestMode&&!m_isPaused);}
};
struct PlayLayer:GJBaseGameLayer{GJGameLevel level;GJGameLevel*m_level=&level;static inline PlayLayer*current=nullptr;static PlayLayer*get(){return current;}};
struct CorumSurvivalReplayLayer:PlayLayer{struct Fields{bool replayOwned=false,liveWorldOwned=false,replayAdvancing=false,survivalReplayLevel=false,survivalLiveLevel=false;}fields;Fields*m_fields=&fields;};using CorumRankedPlayLayer=CorumSurvivalReplayLayer;
static PlayLayer*persistenceOwner=nullptr;struct ScopedPersistenceOwner{PlayLayer*old=persistenceOwner;ScopedPersistenceOwner(PlayLayer*p){persistenceOwner=p;}~ScopedPersistenceOwner(){persistenceOwner=old;}};
struct ScopedRealGameplayEnd{bool&value;bool old;ScopedRealGameplayEnd(bool&v,bool n):value(v),old(v){v=n;}~ScopedRealGameplayEnd(){value=old;}};
namespace corum::ranked{
namespace replay {struct Recorder{std::vector<matjson::Value>records;bool enabled=true;static Recorder&get(){static Recorder r;return r;}bool active()const{return enabled;}bool inNativeFrame()const{return true;}void add(matjson::Value v){records.push_back(v);}};}
struct SurvivalRuntime{bool official=true,free=false;static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}bool gameplayLevel(int id)const{return official&&id==123;}bool freePlaying()const{return free;}};
struct SurvivalSpectatorRuntime{bool worldOwned=false;static SurvivalSpectatorRuntime&get(){static SurvivalSpectatorRuntime s;return s;}bool world()const{return worldOwned;}};
${method(play,'void advanceSurvivalReplayWorld(')}
}
struct Hook:CorumSurvivalReplayLayer{
${method(native,'    void handleButton(')}
${method(native,'    void processQueuedButtons(')}
};
int main(){using namespace corum::ranked;Hook scene;PlayLayer::current=&scene;auto&r=replay::Recorder::get();
 PlayerButtonCommand first;first.m_isPush=true;first.m_isPlayer2=true;first.m_timestamp=.0025;auto future=first;future.m_step=4;scene.m_queuedButtons={first,future};scene.processQueuedButtons(.01f,false);
 assert(scene.consumed==1&&scene.m_queuedButtons.size()==1&&r.records.size()==1);assert(r.records[0]["player2"].asBool().unwrap()&&r.records[0]["down"].asBool().unwrap()&&r.records[0]["timestamp"].asDouble().unwrap()==.0025);
 scene.processQueuedButtons(.01f,false);assert(r.records.size()==1);scene.m_currentStep=4;scene.processQueuedButtons(.01f,false);assert(r.records.size()==2);
 scene.m_queuedButtons={first};SurvivalRuntime::get().free=true;scene.processQueuedButtons(.01f,false);assert(r.records.size()==2);SurvivalRuntime::get().free=false;
 scene.m_isPracticeMode=true;scene.m_queuedButtons={first};scene.processQueuedButtons(.01f,false);assert(r.records.size()==2);scene.m_isPracticeMode=false;
 scene.fields.replayOwned=true;scene.handleButton(true,1,true);assert(scene.nativeInputs==0);scene.fields.replayOwned=false;scene.fields.liveWorldOwned=true;scene.handleButton(true,1,true);assert(scene.nativeInputs==0);scene.fields.replayAdvancing=true;scene.handleButton(true,1,false);assert(scene.nativeInputs==1);scene.fields.replayAdvancing=false;scene.handleButton(false,1,false);assert(scene.nativeInputs==1);scene.fields.liveWorldOwned=false;scene.handleButton(true,1,true);assert(scene.nativeInputs==2);
 scene.m_isPaused=true;scene.fields.survivalReplayLevel=true;advanceSurvivalReplayWorld(&scene,.01f);assert(scene.nativeUpdates==1&&!scene.m_isTestMode&&scene.m_isPaused&&!persistenceOwner);
 scene.fields.survivalReplayLevel=false;advanceSurvivalReplayWorld(&scene,.01f);assert(scene.nativeUpdates==1);scene.fields.survivalLiveLevel=true;SurvivalSpectatorRuntime::get().worldOwned=true;advanceSurvivalReplayWorld(&scene,.01f);assert(scene.nativeUpdates==2&&!scene.m_isTestMode&&scene.m_isPaused);
 Hook foreign;advanceSurvivalReplayWorld(&foreign,.01f);assert(foreign.nativeUpdates==0);
}
`;
 writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-isystem',json+'/include','-isystem',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
