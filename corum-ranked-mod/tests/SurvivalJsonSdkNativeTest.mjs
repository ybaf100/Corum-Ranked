// This regression links the real Geode matjson dependency. In particular,
// default Value is {}, not null as in the old hand-written JSON double.
import test from 'node:test';
import assert from 'node:assert/strict';
import {existsSync,readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const header=readFileSync(process.env.CORUM_RUNTIME_HEADER ?? new URL('../src/SurvivalRuntime.hpp',import.meta.url),'utf8');
const json=process.env.CORUM_MATJSON_SOURCE ?? '/workspace/scratch/geode-cpm-cache/json/f349';
const result=process.env.CORUM_RESULT_INCLUDE ?? '/workspace/scratch/geode-cpm-cache/result/1b9c/include';
test('actual SDK JSON: an idle Force Clear request cannot disable Pause actions or block surrender',()=>{
 assert(existsSync(join(json,'src/value.cpp')),'Set CORUM_MATJSON_SOURCE to the pinned SDK matjson source');
 assert(existsSync(join(result,'Geode/Result.hpp')),'Set CORUM_RESULT_INCLUDE to Geode Result include');
 const field=header.match(/^    (?:matjson::Value|std::optional<matjson::Value>) m_forceClearBody.*?;$/m)?.[0];assert(field);
 const pending=header.match(/bool forceClearPending\(\) const \{[^}]+\}/)?.[0];assert(pending);
 const cpp=`#include <matjson.hpp>
#include <optional>
#include <cassert>
struct Runtime{${field}\n${pending}};
int main(){
 matjson::Value actualDefault;assert(actualDefault.isObject());
 Runtime runtime;assert(!runtime.forceClearPending());
}
`;
 const dir=mkdtempSync(join(tmpdir(),'survival-sdk-json-'));try{
  writeFileSync(join(dir,'test.cpp'),cpp);
  execFileSync('g++',['-std=c++23','-O0','-I',join(json,'include'),'-I',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});

const runtimeSource=readFileSync(process.env.CORUM_RUNTIME_SOURCE ?? new URL('../src/SurvivalRuntime.cpp',import.meta.url),'utf8');
const pauseSource=readFileSync(process.env.CORUM_PAUSE_SOURCE ?? new URL('../src/RankedPauseLayer.cpp',import.meta.url),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
test('actual SDK JSON and production watchBudget keep allowed surrender/Force Clear buttons enabled; pending and forbidden states disable them',()=>{
 const field=header.match(/^    (?:matjson::Value|std::optional<matjson::Value>) m_forceClearBody.*?;$/m)[0];
 const pending=method(header,'bool forceClearPending()');
 const watch=method(pauseSource,'void watchBudget(float)');const survival=watch.slice(0,watch.indexOf('        auto& ranked ='))+'}';
 const code=`
#include <matjson.hpp>
#include <algorithm>
#include <cassert>
#include <optional>
#include <string>
struct Node{bool parent=true;Node*getParent(){return parent?this:nullptr;}};
struct Label{void setString(char const*){}};struct Button{bool enabled=false,visible=true;void setEnabled(bool v){enabled=v;}bool isEnabled(){return enabled;}void setVisible(bool v){visible=v;}};
struct PlayLayer:Node{};
template<class T>struct WeakRef{T*p;struct Ref{T*p;T*data(){return p;}T*operator->(){return p;}explicit operator bool()const{return p;}};Ref lock(){return {p};}};
struct CCDirector{struct Size{float width=568;};static CCDirector*sharedDirector(){static CCDirector d;return &d;}Size getWinSize(){return {};}};
namespace geode::log{template<class...T>void info(T...){}}
namespace log=geode::log;
namespace fmt{template<class...T>std::string format(char const*s,T...){return s;}}
namespace corum::ranked{
struct RankedRuntime{static RankedRuntime&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken()const{return "token";}};
namespace i18n{std::string text(std::string const&,std::string const&s){return s;}}
namespace ui{void fitLabelWidth(Label*,float,float,float){}}
bool requestSurvivalGameplayReturn(PlayLayer*){return true;}
struct SurvivalRuntime{
 struct View{std::string id="m",roundKey="r1",phase="ROUND_1",state="PLAYING",error;int mapGeneration=1;bool debug=true,online=false,ownEliminated=false,ownAssetsReady=true,ownEntered=true;}m_view;
 std::string m_session="token";bool surrendered=false,cleared=false;
 ${field}
 ${pending}
 static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}void tick(){}auto const&view(){return m_view;}
 bool active()const{return true;}bool surrenderPending()const{return surrendered;}bool normalRoundCleared()const{return cleared;}bool freePlaying()const{return false;}
 bool canPlay()const{return m_view.state=="PLAYING";}
 ${method(runtimeSource,'bool SurvivalRuntime::canEndRound()').replace('SurvivalRuntime::','')}
 long remainingMs()const{return 180000;}
 std::optional<long>roundRemainingMs()const{return 180000;}
 ${method(runtimeSource,'bool SurvivalRuntime::canSurrender()').replace('SurvivalRuntime::','')}
 ${method(runtimeSource,'bool SurvivalRuntime::canForceClear()').replace('SurvivalRuntime::','')}
};
struct Pause:Node{
 struct Fields{bool survival=true;Label*budget=nullptr;Label*survivalStatus=nullptr;Button*surrender=nullptr;Button*forceClear=nullptr;Button*endRound=nullptr;
 std::optional<WeakRef<Node>>survivalConfirmation;std::optional<WeakRef<PlayLayer>>owner;
 std::string matchId="m",survivalRoundKey="r1";int survivalMapGeneration=1,survivalWatchLogs=0;}f;
 Fields*m_fields=&f;Label budget;Button surrender,force,end;Pause(){f.budget=&budget;f.surrender=&surrender;f.forceClear=&force;f.endRound=&end;}
 void synchronizeSurvivalPauseInput(){}void setupSurvivalForceClear(CCDirector::Size){}
 ${survival}
};
}
using namespace corum::ranked;
int main(){auto&r=SurvivalRuntime::get();Pause pause;
 assert(r.canSurrender()&&r.canForceClear());assert(!pause.surrender.enabled&&!pause.force.enabled&&!pause.end.enabled);
 pause.watchBudget(0);assert(pause.surrender.enabled&&pause.force.enabled&&pause.end.enabled);
 r.m_view.error="network delay";pause.watchBudget(0);assert(pause.surrender.enabled&&pause.force.enabled&&pause.end.enabled);
 r.m_forceClearBody=matjson::Value::object();assert(r.forceClearPending());pause.watchBudget(0);assert(!pause.surrender.enabled&&!pause.force.enabled&&!pause.end.enabled);
 r=SurvivalRuntime{};pause.watchBudget(0);assert(pause.surrender.enabled&&pause.force.enabled&&pause.end.enabled);
 r.m_view.online=true;pause.watchBudget(0);assert(pause.surrender.enabled&&!pause.force.enabled&&!pause.end.enabled);
 r.m_view.state="CANCELLED";pause.watchBudget(0);assert(!pause.surrender.enabled&&!pause.force.enabled&&!pause.end.enabled);
}
`;
 const dir=mkdtempSync(join(tmpdir(),'survival-sdk-buttons-'));try{
 writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-O0','-DCORUM_RANKED_DEBUG_BOT_MATCH=1','-I',join(json,'include'),'-I',result,join(dir,'test.cpp'),...['value','impl','parser','dump','external/dragonbox'].map(p=>join(json,'src',p+'.cpp')),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}
});
