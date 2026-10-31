import test from 'node:test';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';import {tmpdir} from 'node:os';
const root=resolve(new URL('..',import.meta.url).pathname);
function method(src,name){let a=src.indexOf(name),b=src.indexOf('{',a)+1,d=1;if(a<0)throw Error(name);while(d){d+=(src[b]==='{')-(src[b]==='}');++b;}return src.slice(a,b);}
test('production replay/native init loads Source LDM before object creation, restores preferences on failure, and keeps legacy maps strict',()=>{
 const json=process.env.CORUM_MATJSON_SOURCE,result=process.env.CORUM_RESULT_INCLUDE;if(!json||!result)throw Error('Pinned JSON dependencies required');
 const native=readFileSync(join(root,'src/replay/SurvivalReplayNative.cpp'),'utf8');
 const body=method(native,'bool init(GJGameLevel* level,bool useReplay,bool noObjects)').replaceAll('log::error','fixtureLog::error');
 const dir=mkdtempSync(join(tmpdir(),'replay-world-options-'));try{
 writeFileSync(join(dir,'test.cpp'),`
#include "replay/SurvivalReplayWorldOptions.hpp"
#include "replay/SurvivalReplayCodec.hpp"
#include <cassert>
#include <utility>
using Json=matjson::Value;using namespace corum::ranked::replay;
struct GJGameLevel{int m_levelID=1;bool m_lowDetailModeToggled=false,m_disableShakeToggled=false;};
struct Playback{Json init;Json const&initial()const{return init;}};
Playback tape;
namespace corum::ranked {
namespace replay{struct Runtime{bool owned=true;int exits=0;static auto&get(){static Runtime r;return r;}bool owns(int)const{return owned;}void exited(){++exits;}};bool cbfReplayHooksHeld(){return false;}void releaseCbfReplayHooks(){}}
struct SurvivalSpectatorRuntime{bool live=false;int exits=0;static auto&get(){static SurvivalSpectatorRuntime r;return r;}bool sceneLevel(int)const{return live;}Playback*world(){return live?&tape:nullptr;}void sceneExited(){++exits;}};
namespace i18n{std::string text(char const*,char const* text){return text;}}
}
namespace fixtureLog{template<class...T>void error(T...){} }
struct FLAlertLayer{template<class...T>static auto*create(T...){static FLAlertLayer a;return &a;}void show(){}};
template<class F>void queueInMainThread(F f){f();}
struct PlayLayer{bool m_isTestMode=false,loadedLow=false,loadedShake=false,fail=false;int objects=0;
 bool init(GJGameLevel*level,bool,bool){loadedLow=level->m_lowDetailModeToggled;loadedShake=level->m_disableShakeToggled;objects=loadedLow?10:20;return !fail;}};
struct Hook:PlayLayer{struct Fields{bool replayOwned=false,liveWorldOwned=false,ready=false;}fields,*m_fields=&fields;int resets=0,hud=0;
 Playback*worldPlayback(){return &tape;}void resetReplayInitial(){++resets;}void addReplayHud(){++hud;}
 ${body}
};
int main(){
 auto& runtime=corum::ranked::replay::Runtime::get();auto& live=corum::ranked::SurvivalSpectatorRuntime::get();
 for(bool ldm:{false,true})for(bool shake:{false,true}){
  tape.init["worldOptions"]=replayWorldOptions(ldm,shake);
  GJGameLevel level;level.m_lowDetailModeToggled=!ldm;level.m_disableShakeToggled=!shake;
  Hook replay;assert(replay.init(&level,false,false));assert(replay.loadedLow==ldm&&replay.loadedShake==shake&&replay.objects==(ldm?10:20));
  assert(replay.resets==1&&replay.hud==1&&replay.m_isTestMode&&replay.fields.ready);
  assert(level.m_lowDetailModeToggled==!ldm&&level.m_disableShakeToggled==!shake);
  Hook failed;failed.fail=true;assert(!failed.init(&level,false,false));assert(!failed.fields.ready);
  assert(level.m_lowDetailModeToggled==!ldm&&level.m_disableShakeToggled==!shake);
  runtime.owned=false;live.live=true;Hook viewer;assert(viewer.init(&level,false,false));assert(viewer.loadedLow==ldm&&viewer.resets==1&&!viewer.m_isTestMode);
  live.live=false;runtime.owned=true;
  // The exact same scoped options surround reset/seek without persisting preferences.
  {ReplayWorldOptionsScope reset(&level,tape.initial()["worldOptions"]);assert(level.m_lowDetailModeToggled==ldm);}
  assert(level.m_lowDetailModeToggled==!ldm);
  assert(replayWorldOptionsMismatch(tape.initial()["worldOptions"],ldm).empty());
  assert(replayWorldOptionsMismatch(tape.initial()["worldOptions"],!ldm)=="INITIAL_WORLD_OPTIONS_MISMATCH");
 }
 // Legacy data remains unknown; it must not be inferred as ON or OFF.
 tape.init=Json::object();GJGameLevel level;level.m_lowDetailModeToggled=true;
 Hook old;assert(old.init(&level,false,false)&&old.loadedLow&&level.m_lowDetailModeToggled);
 assert(replayWorldOptionsProblem(Json(nullptr)).empty());auto malformed=replayWorldOptions(true,false);malformed["lowDetail"]="true";
 assert(replayWorldOptionsProblem(malformed)=="INITIAL_WORLD_OPTIONS_INVALID");
 Json initial;initial["worldOptions"]=malformed;assert(replayInitialDataProblem(initial)=="INITIAL_WORLD_OPTIONS_INVALID");
 {ReplayWorldOptionsScope invalid(&level,malformed);assert(level.m_lowDetailModeToggled);}
}
`);
 execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-isystem',json+'/include','-isystem',result,'-I',join(root,'src'),join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>json+'/src/'+p+'.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
