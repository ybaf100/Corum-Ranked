// Production presentation/navigation/options at native boundaries. These are
// not device rendering, real Shake Trigger, or FMOD teardown tests.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');++end;}return source.slice(start,end);}
function run(source,units=[]){const dir=mkdtempSync(join(tmpdir(),'survival-board-'));try{
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),...units.map(p=>join(root,p)),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});
}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('64-player density follows the presented round, including the complete elimination announcement',()=>run(`
#include "domain/SurvivalBoardPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 for(auto phase:{"ROUND_1","ROUND_2"})for(int alive:{64,40,26,12}){
  auto g=survivalGridLayout(568,320,64,alive,phase,64);assert(g.compact&&!g.reduced&&g.pages==1&&g.pageSize>=64);
 }
 for(int alive:{40,26,12,5}){auto g=survivalGridLayout(568,320,26,alive,"ROUND_3",64);assert(g.reduced&&!g.compact);}
 for(auto phase:{"ROUND_4","FINAL","TIEBREAK"}){auto g=survivalGridLayout(568,320,12,12,phase,64);assert(!g.reduced&&!g.compact);}
 for(auto phase:{"ROUND_1","ROUND_2","ROUND_3","FINAL"})assert(survivalPhaseCardDensity(16,phase)==SurvivalCardDensity::Full);
}
`));

test('board uses server deadlines through clear, network gaps, countdown, background elapsed time and result',()=>{
 run(`#include "domain/SurvivalBoardPresentation.hpp"
#include "domain/ServerClock.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){ServerClock clock;auto now=0LL;
 auto ends=[&]{return clock.remainingMillis("2026-10-16T00:03:00Z",now);};
 auto starts=[&]{return clock.signedRemainingMillis("2026-10-16T00:00:10Z",now);};
 assert(survivalBoardClock("PLAYING",starts(),ends())=="ROUND --:--");
 clock.observe("2026-10-16T00:00:00Z",now);
 assert(survivalBoardClock("COUNTDOWN",starts(),ends())=="START IN 00:10");
 now=1001;assert(survivalBoardClock("COUNTDOWN",starts(),ends())=="START IN 00:09");
 now=11000;assert(survivalBoardClock("COUNTDOWN",starts(),ends())=="START IN 00:00");
 assert(survivalBoardClock("PLAYING",starts(),ends())=="ROUND 02:49");
 // A 100% ACK, poll error or Free Play changes neither the shared server clock
 // nor endsAt. Display elapsed time while no response is arriving.
 now+=61000;assert(survivalBoardClock("PLAYING",starts(),ends())=="ROUND 01:48");
 assert(!clock.observe("invalid",now));now+=1000;
 assert(survivalBoardClock("PLAYING",starts(),ends())=="ROUND 01:47");
 now+=500000;assert(survivalBoardClock("PLAYING",starts(),ends())=="ROUND 00:00");
 assert(survivalBoardClock("ROUND_RESULT",starts(),ends())=="RESULT");
 for(auto const* state:{"FINAL_RESULT","CANCELLED",""})assert(survivalBoardClock(state,starts(),ends()).empty());
 assert(survivalBoardClock("MAP_SELECTION",{},{} )=="SELECTING MAP");
 assert(survivalBoardClock("PREPARING",{},{} )=="PREPARING");
 assert(survivalBoardClock("ENTERING",{},{} )=="WAITING FOR PLAYERS");
 clock.observe("2026-10-16T00:00:00Z",now);
 assert(survivalBoardClock("COUNTDOWN",starts(),ends())=="START IN 00:10");
}`,['src/domain/ServerClock.cpp']);
 const ui=read('src/ui/SurvivalGameLayer.cpp');
 assert.equal((ui.match(/m_boardClock=ui::menuLabel/g)||[]).length,1);
 assert.match(ui,/survivalBoardClock\(view.state,runtime.countdownMs\(\),runtime.roundRemainingMs\(\)\)/);
 assert.match(ui,/m_boardClock->setVisible\(!result && !elimination && !clock.empty\(\)\)/);
});

test('Back from map navigates to the retained board without forfeiting; another Back requires confirmation',()=>{
 const back=method(read('src/ui/SurvivalGameLayer.cpp'),'void keyBackClicked()').replace(' override','');
 run(`#include <cassert>
#include <chrono>
struct Result{bool exists=false;bool isObject()const{return exists;}};
enum class SurvivalFlowStage{Active,EliminatedChoice,PersonalResult,Ceremony};
enum class SurvivalOutcomeChoice{Observe,PersonalResult};
struct SurvivalRuntime{
 struct View{Result result;bool ownEliminated=false;}v;
 bool cleared=false,observe=false;int acknowledgments=0,choices=0;
 SurvivalFlowStage stage=SurvivalFlowStage::Active;
 SurvivalOutcomeChoice lastChoice=SurvivalOutcomeChoice::Observe;
 static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}
 auto const&view(){return v;}
 bool normalRoundCleared(){return cleared;}
 void acknowledge(){++acknowledgments;}
 SurvivalFlowStage flowStage()const{return stage;}
 bool observing()const{return observe;}
 void chooseOutcome(SurvivalOutcomeChoice choice){++choices;lastChoice=choice;}
 void leaveObserver(){observe=false;}
};
struct Coordinator{std::uint64_t m_boardVisit=0;bool m_closed=false,m_motion=false,m_browsingBoard=false,m_manualMap=false;std::chrono::steady_clock::time_point m_eliminationUntil{};int confirmations=0;void confirmSurrender(){++confirmations;}
 ${back}
};
int main(){Coordinator c;auto&r=SurvivalRuntime::get();c.keyBackClicked();assert(c.m_browsingBoard&&c.m_manualMap&&c.confirmations==0&&r.acknowledgments==0&&!c.m_closed);
 c.keyBackClicked();assert(c.confirmations==1&&!c.m_closed);r.v.ownEliminated=true;c.keyBackClicked();assert(c.confirmations==1);
 c.m_browsingBoard=false;c.m_manualMap=false;r.cleared=true;r.v.ownEliminated=false;c.keyBackClicked();assert(c.confirmations==2);
 c.m_manualMap=true;c.keyBackClicked();assert(c.m_browsingBoard&&c.confirmations==2);
 c.m_motion=true;c.keyBackClicked();assert(c.confirmations==2);c.m_motion=false;
 r.v.result.exists=true;c.keyBackClicked();assert(r.acknowledgments==1&&!c.m_closed);
 // Eliminated choice and local observation always route to the personal
 // result rather than sending a second surrender/forfeit or acknowledging
 // the shared match result.
 r.v.result.exists=false;r.stage=SurvivalFlowStage::EliminatedChoice;
 const auto beforeConfirm=c.confirmations;
 c.keyBackClicked();assert(r.choices==1&&r.lastChoice==SurvivalOutcomeChoice::PersonalResult);
 assert(c.confirmations==beforeConfirm&&r.acknowledgments==1);
 r.stage=SurvivalFlowStage::Active;r.observe=true;
 c.keyBackClicked();assert(r.choices==2&&r.lastChoice==SurvivalOutcomeChoice::PersonalResult);
 assert(c.confirmations==beforeConfirm&&r.acknowledgments==1);
}`);
 const ui=read('src/ui/SurvivalGameLayer.cpp');
 assert.match(ui,/m_entrySyncAfter=runtime.serverSyncMillis\(\);runtime.refreshRound\(\)/);
 assert.match(ui,/m_freePlay->setVisible\(liveBoard && runtime.canFreePlay\(\)\)/);
});

test('native LDM capability and saved options survive object/map replacement without exposing unknown LDM',()=>{
 run(`#include "domain/SurvivalNativeOptions.hpp"
#include <cassert>
using namespace corum::ranked;
struct Level{int m_levelID=10;bool m_lowDetailMode=false,m_lowDetailModeToggled=false,m_disableShakeToggled=false;};
int main(){Level a,b;bool prefer=true,shake=true;
 assert(!survivalSupportsLowDetail<Level>(nullptr,10,true));
 assert(!survivalSupportsLowDetail(&a,10,true));a.m_lowDetailMode=true;
 assert(!survivalSupportsLowDetail(&a,10,false));assert(!survivalSupportsLowDetail(&a,11,true));
 assert(survivalSupportsLowDetail(&a,10,true));
 applySurvivalNativeOptions(&a,prefer,shake);assert(a.m_lowDetailModeToggled&&a.m_disableShakeToggled);
 applySurvivalNativeOptions(&b,prefer,shake);assert(!b.m_lowDetailModeToggled&&b.m_disableShakeToggled&&prefer);
 b.m_lowDetailMode=true;applySurvivalNativeOptions(&b,prefer,shake);assert(b.m_lowDetailModeToggled);
 applySurvivalNativeOptions(&b,false,false);assert(!b.m_lowDetailModeToggled&&!b.m_disableShakeToggled);
 applySurvivalNativeOptions<Level>(nullptr,true,true);
}`);
 const prepare=read('src/RankedRoundPreparation.cpp'),play=read('src/PlayLayer.cpp');
 assert.match(method(prepare,'void RankedRoundPreparation::pump()'),/if\(survival\)corum::ranked::SurvivalOptionsPreferences::get\(\).apply/);
 const launch=method(prepare,'bool RankedRoundPreparation::launch(');
 assert(launch.indexOf('SurvivalOptionsPreferences::get().apply')<launch.indexOf('PlayLayer::scene('));
 const init=method(play,'bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects)');
 assert(init.indexOf('SurvivalOptionsPreferences::get().apply')<init.indexOf('PlayLayer::init('));
 assert.match(init,/m_skipCameraShake=m_skipCameraShake \|\| level->m_disableShakeToggled/);
 assert.match(init,/#if defined\(CORUM_RANKED_DEBUG_BOT_MATCH\)[\s\S]*SurvivalOptions/);
});

test('length prioritizes trusted seconds over every native category and rejects unknown/invalid values',()=>{
 run(`#include "domain/MapMetadata.hpp"
#include <cassert>
#include <limits>
using namespace corum::ranked;
int main(){char const* labels[]{"TINY","SHORT","MEDIUM","LONG","XL"};
 for(int i=0;i<5;i++){assert(mapLengthText({},i)==labels[i]);assert(mapLengthText(154,i)=="2m 34s");}
 for(int i:{-2,-1,5,10})assert(mapLengthText({},i)=="UNKNOWN LENGTH");
 for(double v:{-1.,0.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity(),9e20}){
  assert(mapLengthText(v)=="UNKNOWN LENGTH");assert(mapLengthText(v,4)=="XL");
 }
 MapMetadata first;first.lengthCategory=2;assert(mapLengthText(first)=="MEDIUM");MapMetadata next;assert(mapLengthText(next)=="UNKNOWN LENGTH");
}`);
});

test('native environment policy keeps default TRUE and restores checks without erasing policy',()=>{
 run(`#include "domain/EnvironmentPolicy.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){EnvironmentPolicy p;p.cbfModId="cbf";p.allowedMods.push_back({"required","Required","2",{},true,true});
 std::vector<InstalledModSnapshot> mods{{"other","Other","1",true,true,false,false,{}}};
 assert(p.modRestrictionEnabled&&!evaluateEnvironment(mods,p).allowed);
 p.modRestrictionEnabled=false;auto off=evaluateEnvironment(mods,p);assert(off.allowed&&off.unauthorizedModIds.empty()&&off.missingRequiredModIds.empty()&&off.cbfIssues.empty());
 assert(p.allowedMods.size()==1&&p.allowedMods[0].required&&p.allowedMods[0].minVersion=="2");
 p.modRestrictionEnabled=true;assert(!evaluateEnvironment(mods,p).allowed);p.modRestrictionEnabled=false;assert(evaluateEnvironment({},p).allowed);
}`,['src/domain/EnvironmentPolicy.cpp']);
});


test('native re-entry rejects unknown/expired playing time, stale contexts, eliminated players and DM attempt exhaustion',()=>{
 const runtime=read('src/SurvivalRuntime.cpp');
 run(`#include "domain/LevelPreparationPolicy.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>
using namespace corum::ranked;
struct SurvivalRuntime{
 struct View{std::string state="ENTERING",phase="ROUND_1",ownId="a";bool online=true,ownAssetsReady=true,ownEliminated=false;std::vector<SurvivalStanding>standings;}m_view;
 bool pending=false,cleared=false,free=false;long remaining=100;int m_localAttempts=0;std::string m_session="token";
 LevelPreparationContext current{"match",42,1,0,false,0,1},m_prepared=current;
 bool surrenderPending()const{return pending;}bool normalRoundCleared()const{return cleared;}bool freePlaying()const{return free;}
 std::optional<std::int64_t> wait=0,panelWait=0;bool entered=false;auto entryWaitMs()const{return wait;}auto selectedPanelWaitMs()const{return panelWait;}bool previouslyEntered()const{return entered;}
 auto preparationContext()const{return current;}long remainingMs()const{return remaining;}
 bool attemptLimitReached()const;bool canEnter()const;
 ${method(runtime,'bool SurvivalRuntime::attemptLimitReached()').replace('bool SurvivalRuntime::','bool ')}
 ${method(runtime,'bool SurvivalRuntime::canEnter()').replace('bool SurvivalRuntime::','bool ')}
};
int main(){SurvivalRuntime r;
 r.wait.reset();assert(!r.canEnter());r.wait=3000;assert(!r.canEnter());r.wait=1;assert(!r.canEnter());r.wait=0;assert(r.canEnter());r.wait=-100;assert(r.canEnter());
 r.panelWait=3000;assert(!r.canEnter());r.panelWait.reset();assert(!r.canEnter());r.entered=true;assert(r.canEnter());r.entered=false;r.panelWait=0;
 for(auto const* state:{"COUNTDOWN","PLAYING"}){
  r.m_view.state=state;r.panelWait=1;assert(r.canEnter()==(std::string(state)=="PLAYING")); // Only pre-start entry retains the visible hold.
  r.panelWait.reset();assert(r.canEnter()==(std::string(state)=="PLAYING"));r.panelWait=0;assert(r.canEnter());
  r.panelWait=1;r.entered=true;assert(r.canEnter());r.entered=false;r.panelWait=0;
 }r.m_view.state="ENTERING";
 for(auto const* phase:{"ROUND_1","ROUND_2","ROUND_3","FINAL","TIEBREAK"}){r.m_view.phase=phase;assert(r.canEnter());}
 r.m_view.state="PLAYING";assert(r.canEnter());r.remaining=0;assert(!r.canEnter());r.remaining=1;
 r.m_localAttempts=3;assert(!r.canEnter());r.m_view.phase="ROUND_1";assert(r.canEnter());
 r.pending=true;assert(!r.canEnter());r.pending=false;r.cleared=true;assert(!r.canEnter());r.cleared=false;r.free=true;assert(!r.canEnter());r.free=false;
 r.current.map++;assert(!r.canEnter());r.m_prepared=r.current;r.m_view.ownEliminated=true;assert(!r.canEnter());r.m_view.ownEliminated=false;
 for(auto const* state:{"ROUND_RESULT","MAP_SELECTION","PREPARING","FINAL_RESULT","CANCELLED"}){r.m_view.state=state;assert(!r.canEnter());}
}`.replace(/ bool attemptLimitReached\(\)const;bool canEnter\(\)const;/,''));
});


test('Geode can load without policy-optional packages; server TRUE keeps their required rules',()=>{
 const manifest=JSON.parse(read('mod.json'));
 for(const id of ['syzzi.click_between_frames','geode.node-ids']){
  assert.equal(manifest.dependencies[id].required,false);
  assert.match(manifest.dependencies[id].version,/^>=v/);
 }
});


test('policy polling and Recheck update both directions without restarting sessions or losing a match',()=>{
 const runtime=read('src/RankedRuntime.cpp'),viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
 const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
 run(`#include "domain/EnvironmentPolicy.hpp"
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>
${json}
using namespace corum::ranked;
namespace web{struct WebResponse{matjson::Value root;bool okay=true;matjson::Result<matjson::Value>json()const{return {root};}};}
std::vector<std::function<void(web::WebResponse)>> pending;
struct Request{void spawn(int,std::function<void(web::WebResponse)> f){pending.push_back(f);}};
struct Http{void timeout(std::chrono::seconds){}int get(std::string const&){return 1;}};
Http baseRequest(std::string const&){return {};}std::string endpoint(std::string const& p){return p;}bool successful(web::WebResponse const&r){return r.okay;}
enum class RuntimeStage{Blocked,Matched,Ready};
struct RankedRuntime{
 bool m_rankedOpen=true,m_sessionRejected=false,m_modeConfigBusy=false;std::string m_sessionToken="token";std::uint64_t m_sessionGeneration=1;
 std::chrono::steady_clock::time_point m_nextModeConfigAt{};Request m_modeConfigRequest;EnvironmentPolicy m_environmentPolicy;std::vector<InstalledModSnapshot> m_installedMods;
 struct{RuntimeStage stage=RuntimeStage::Matched;std::string error;int revision=0;bool soloEnabled=false,survivalEnabled=false,survivalConfigReady=false;int survivalPlayerCount=16;
  struct{bool active=false;}environmentWarning;struct{std::string matchId="retained";}match;}m_view;
 int configLoads=0;void fetchConfig(){++configLoads;}auto captureInstalledMods(){return std::vector<InstalledModSnapshot>{};}
 void setStage(RuntimeStage s,std::string const&){m_view.stage=s;}
 void showEnvironmentWarning(EnvironmentDecision const&,bool block){m_view.environmentWarning.active=true;if(block)m_view.stage=RuntimeStage::Blocked;}
 void fetchModeAvailability();void recheckEnvironment();
};
${method(runtime,'void RankedRuntime::fetchModeAvailability()')}
${method(runtime,'void RankedRuntime::recheckEnvironment()')}
int main(){RankedRuntime r;
 auto reply=[&](bool enabled){r.fetchModeAvailability();assert(r.m_modeConfigBusy);matjson::Value root;root["modRestrictionEnabled"]=enabled;pending.back()({root});assert(!r.m_modeConfigBusy);};
 reply(false);assert(!r.m_environmentPolicy.modRestrictionEnabled&&!r.m_view.environmentWarning.active&&r.m_view.stage==RuntimeStage::Matched&&r.m_view.match.matchId=="retained"&&r.m_sessionToken=="token");
 reply(true);assert(r.m_view.environmentWarning.active&&r.m_view.stage==RuntimeStage::Matched);
 r.m_view.stage=RuntimeStage::Blocked;reply(false);assert(r.m_view.stage==RuntimeStage::Matched&&!r.m_view.environmentWarning.active);
 r.m_view.stage=RuntimeStage::Blocked;r.recheckEnvironment();assert(r.m_view.stage==RuntimeStage::Matched&&r.configLoads==0);
 r.m_sessionToken.clear();r.m_view.stage=RuntimeStage::Blocked;r.recheckEnvironment();assert(r.configLoads==1&&r.m_view.match.matchId=="retained");
 r.m_sessionToken="token";r.fetchModeAvailability();auto stale=pending.back();++r.m_sessionGeneration;matjson::Value root;root["modRestrictionEnabled"]=true;stale({root});assert(!r.m_environmentPolicy.modRestrictionEnabled);
}`,['src/domain/EnvironmentPolicy.cpp']);
});


test('dense board card rows stay inside their borders with a separate avatar slot',()=>{
 run(`#include "domain/SurvivalBoardPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 for(float width:{85.f,110.f,120.f,150.f})for(float height:{36.f,42.f,48.f,66.f}){
  auto c=survivalCardLayout(width,height);
  assert(c.titleY+c.titleHeight/2<=c.height-4+.001f);
  assert(c.recordY-c.recordHeight/2>=4-.001f);
  assert(c.avatarY+c.avatarSize/2<c.titleY-c.titleHeight/2);
  assert(c.avatarY-c.avatarSize/2>c.recordY+c.recordHeight/2);
 }
}`);
});


test('all 64 compact waiting cards fit one screen with nickname and icon geometry bounded',()=>{
 run(`#include "domain/SurvivalBoardPresentation.hpp"
#include "domain/SurvivalRoundMenuPolicy.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 for(float width:{320.f,420.f,568.f,768.f,1366.f})for(float height:{240.f,320.f,426.f,768.f})for(std::size_t count:{5u,12u,26u,40u,64u}){
  auto g=survivalGridLayout(width,height,count,64);auto c=survivalCardLayout(g.cellWidth,g.cellHeight,true);
  assert(g.compact&&g.pages==1&&g.pageSize>=count&&g.cellHeight>4);
  assert(g.columns*g.cellWidth<=width-36+.01f);assert(g.rows*g.cellHeight<=height-133+.01f);
  assert(c.avatarX-c.avatarSize/2>=0&&c.avatarX+c.avatarSize/2<c.titleX-c.titleWidth/2);
  assert(c.titleX+c.titleWidth/2<=c.width&&c.titleHeight>0&&c.recordHeight==0);
 }
 auto compact=survivalGridLayout(568,320,64,64);assert(compact.pages==1&&compact.pageSize==64&&compact.columns==8&&compact.rows==8);
 auto wide=survivalGridLayout(1024,768,64,64);assert(wide.pages==1&&wide.pageSize==64);
 for(auto const* phase:{"ROUND_1","ROUND_2","ROUND_3","ROUND_4"})assert(survivalComparisonScore(phase,42.,99.)==42.);
 for(auto const* phase:{"FINAL","TIEBREAK"})assert(survivalComparisonScore(phase,42.,99.)==99.);
 assert(!survivalComparisonScore("ROUND_4",{},99.));assert(!survivalComparisonScore("FINAL",42.,{}));
 auto original=survivalGridLayout(568,320,16,16);assert(!original.compact&&original.pages==1&&original.columns==4);
 assert(survivalMenuMusic("ROUND_3",false,false,false,true,"ROUND_3",true,true,64)==SurvivalMenuMusic::Round);
 assert(survivalMenuMusic("ROUND_4",false,false,false,true,"ROUND_4",true,true,64)==SurvivalMenuMusic::Final);
 assert(survivalMenuMusic("ROUND_3",false,false,false,true,"ROUND_3",true,true,16)==SurvivalMenuMusic::Final);
}
`);
});
