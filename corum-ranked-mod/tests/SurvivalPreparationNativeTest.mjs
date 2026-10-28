import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {execFileSync} from 'node:child_process';
const root=resolve(new URL('..',import.meta.url).pathname),read=p=>readFileSync(join(root,p),'utf8');
function method(source,signature){const a=source.indexOf(signature);assert(a>=0,signature);let p=source.indexOf('{',a)+1,n=1;while(n){n+=(source[p]==='{')-(source[p]==='}');++p;}return source.slice(a,p);}
function run(cpp){const d=mkdtempSync(join(tmpdir(),'survival-preparation-'));try{writeFileSync(join(d,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(d,'test.cpp'),'-o',join(d,'test')],{stdio:'pipe'});execFileSync(join(d,'test'),[],{stdio:'pipe'});}catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(d,{recursive:true,force:true});}}

test('production launch callback preserves first-entry authority while browsing, fences stale contexts/modals and launches each automatic context once',()=>{
 const source=read('src/ui/SurvivalGameLayer.cpp');
 run(`
#include <cassert>
#include <functional>
#include <utility>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
struct Context{int generation=1;bool operator==(Context const&)const=default;};std::string preparationContextText(Context c){return "m:r:"+std::to_string(c.generation)+":123";}
struct Scene{};Scene scene;struct CCDirector{bool changing=false;static auto*sharedDirector(){static CCDirector d;return &d;}Scene*getRunningScene(){return &scene;}bool getNextScene(){return changing;}};
std::vector<std::function<void()>>tasks;template<class F>void queueInMainThread(F f){tasks.push_back(f);}void drain(){auto pending=std::exchange(tasks,{});for(auto&f:pending)f();}
template<class T>struct WeakRef{T*p;WeakRef(T*p):p(p){}T*lock()const{return p;}};struct Popup{bool getParent(){return true;}};
struct SurvivalRuntime{Context context;bool eligible=true,surrender=false;static auto&get(){static SurvivalRuntime r;return r;}Context preparationContext(){return context;}bool surrenderPending(){return surrender;}bool canEnter(){return eligible;}bool canEnterObserver(){return false;}bool canReplayCleared(){return false;}long serverSyncMillis(){return 99;}};
struct RankedRoundPreparation{int launches=0;bool activePlay=false;static auto&get(){static RankedRoundPreparation r;return r;}bool launch(Scene*){if(activePlay)return false;++launches;return true;}};
struct Selection{bool ready=true;bool settled(){return ready;}};
struct Coordinator{Selection selection;Selection*m_selection=&selection;bool m_closed=false,m_motion=false,m_browsingBoard=false,m_manualMap=false,m_confirmation=false;
 std::optional<WeakRef<Popup>>m_jukebox;std::optional<long>m_entrySyncAfter;std::uint64_t m_boardVisit=0;std::string m_pendingLaunch,m_launchedContext,m_replayRequestedContext;
 bool isRunning(){return true;}Scene*getParent(){return &scene;}
 ${method(source,'void queueNativeLaunch(')}
};
int main(){Coordinator c;auto&r=SurvivalRuntime::get();auto&p=RankedRoundPreparation::get();
 c.queueNativeLaunch(true);c.m_browsingBoard=true;drain();assert(p.launches==1&&c.m_pendingLaunch.empty());
 for(int i=0;i<20;i++){c.queueNativeLaunch(true);drain();}assert(p.launches==1);
 ++r.context.generation;c.queueNativeLaunch(true);++r.context.generation;drain();assert(p.launches==1);
 c.queueNativeLaunch(true);c.m_confirmation=true;drain();assert(p.launches==1);c.m_confirmation=false;
 r.surrender=true;c.queueNativeLaunch(true);drain();assert(p.launches==1);r.surrender=false;
 r.eligible=false;c.queueNativeLaunch(true);drain();assert(p.launches==1);r.eligible=true;
 c.queueNativeLaunch(true);c.m_manualMap=true;drain();assert(p.launches==1);c.m_manualMap=false;
 p.activePlay=true;c.queueNativeLaunch(true);drain();assert(p.launches==1);p.activePlay=false;
 c.queueNativeLaunch(true);drain();assert(p.launches==2&&c.m_browsingBoard);
}
 `);
 const action=method(source,'void handleAction(');
 assert.match(action,/if\(tag==9\)\{m_browsingBoard=true;\}/);
 assert.match(source,/selectedPanelReady\(preparationWindow && m_selection->settled\(\)/);
 assert.match(source,/preparationWindow && !runtime.previouslyEntered\(\)/);
});

test('production logical reveal advances while its visual panel is hidden and retains elapsed selection across board visits and ID resolution',()=>{
 const source=read('src/ui/SurvivalMapSelectionLayer.cpp');
 run(`
#include "domain/SurvivalMapSelection.hpp"
#include <cassert>
#include <map>
using namespace corum::ranked;
long now=10000;long monotonicMs(){return now;}
struct Mod{std::map<std::string,std::string>saved;static auto*get(){static Mod m;return &m;}template<class T>T getSavedValue(std::string k){return saved[k];}void setSavedValue(std::string k,std::string v){saved[k]=v;}};
struct RevealRecord{long began=0;std::size_t consumed=0;bool complete=false,centerPlayed=false;SurvivalRevealModeChoice mode{};SurvivalThumbnailReel reel{};long resolvedAt=0;};std::map<std::string,RevealRecord>reveals;
struct SurvivalMatchView{std::string id="m",roundKey="r",state="PREPARING";int mapGeneration=1;struct{int levelId=123;}map;};
struct SurvivalMapSelectionLayer{SurvivalMapSelectionTimeline m_timeline;std::uint64_t m_sceneGeneration=1;int resets=0;void resetVisuals(){++resets;}
 bool advanceTimeline(SurvivalMatchView const&,std::optional<std::int64_t>);void advancePreparation(SurvivalMatchView const&,std::optional<std::int64_t>);bool settled()const;
};
${method(source,'bool SurvivalMapSelectionLayer::advanceTimeline(')}
${method(source,'void SurvivalMapSelectionLayer::advancePreparation(')}
${method(source,'bool SurvivalMapSelectionLayer::settled()')}
int main(){SurvivalMapSelectionLayer panel;SurvivalMatchView v;panel.advancePreparation(v,{});assert(!panel.settled());now=12000;panel.advancePreparation(v,{});assert(!panel.settled());now=14001;panel.advancePreparation(v,{});assert(panel.settled());auto began=panel.m_timeline.frame(now).elapsed;
 now=18000;panel.advancePreparation(v,{});assert(panel.settled()&&panel.m_timeline.frame(now).elapsed==began&&panel.resets==1);
 v.roundKey="r2";v.map.levelId=0;panel.advancePreparation(v,{});assert(!panel.settled());now+=5000;v.map.levelId=456;panel.advancePreparation(v,{});assert(!panel.settled());now+=2100;panel.advancePreparation(v,{});assert(panel.settled());
 ++v.mapGeneration;panel.advancePreparation(v,{});assert(!panel.settled());v.state="PLAYING";panel.advancePreparation(v,{});assert(panel.settled());v.state="CANCELLED";panel.advancePreparation(v,{});assert(!panel.settled());
}
 `);
});

test('Free Play menu clock consumes server deadlines and hides expired rounds rather than starting a local three-minute timer',()=>run(`
#include "domain/SurvivalMenuClock.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){auto clock=[](std::string state,std::string phase,int round,long remaining,long age=100){return survivalMenuClock(true,true,state,phase,round,remaining,2500,age);};
 auto a=clock("PLAYING","ROUND_2",2,154000);assert(a.visible&&a.phase=="R2"&&a.text=="2:34");
 auto b=clock("PLAYING","ROUND_2",2,93000);assert(b.text=="1:33");assert(clock("PLAYING","FINAL",4,93000).phase=="FINAL");assert(clock("PLAYING","TIEBREAK",5,93000).phase=="DM");
 assert(clock("COUNTDOWN","ROUND_1",1,0).text=="GO 0:03");assert(clock("PREPARING","ROUND_1",1,0).text=="PREPARING");
 for(auto state:{"ROUND_RESULT","FINAL_RESULT","CANCELLED"})assert(!clock(state,"ROUND_2",2,0).visible);
 assert(clock("PLAYING","ROUND_2",2,100000,16000).stale);assert(!survivalMenuClock(true,false,"PLAYING","ROUND_1",1,180000,{},0).visible);
 assert(survivalMenuClock(true,true,"PLAYING","ROUND_1",1,{},{},0).text=="SYNCING");
}
`));

test('actual menu clock placement respects transformed/disabled third-party buttons, 4:3/mobile/wide windows and has no input node',()=>{
 const source=read('src/MenuLayer.cpp');assert.match(source,/#include "ui\/RankedTranslations\.hpp"/);run(`
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};
struct CCRect{CCPoint origin;CCSize size;CCRect(float x,float y,float w,float h):origin{x,y},size{w,h}{}bool intersectsRect(CCRect const& b)const{return origin.x< b.origin.x+b.size.width && b.origin.x<origin.x+size.width && origin.y<b.origin.y+b.size.height && b.origin.y<origin.y+size.height;}};
struct CCNode;struct Children{std::vector<CCNode*>nodes;unsigned count(){return nodes.size();}CCNode*objectAtIndex(unsigned i){return nodes[i];}};
struct CCNode{virtual~CCNode()=default;CCNode*parent=nullptr;Children children;CCPoint pos;CCSize size;float angle=0,sx=1,sy=1;bool visible=true;
 bool isVisible(){return visible;}CCSize getContentSize(){return size;}Children*getChildren(){return &children;}void setPosition(CCPoint p){pos=p;}void setVisible(bool v){visible=v;}
 CCPoint convertToWorldSpace(CCPoint p){float x=p.x*sx,y=p.y*sy;p={pos.x+x*std::cos(angle)-y*std::sin(angle),pos.y+x*std::sin(angle)+y*std::cos(angle)};return parent?parent->convertToWorldSpace(p):p;}
 void add(CCNode*child){children.nodes.push_back(child);child->parent=this;}
};
struct CCMenuItem:CCNode{bool enabled=true;};template<class T>T typeinfo_cast(CCNode*n){return dynamic_cast<T>(n);}
${method(source,'CCRect menuWorldBounds(')}
${method(source,'bool buttonOverlaps(')}
${method(source,'void placeSurvivalMenuClock(')}
int main(){for(auto win:{CCSize{720,540},CCSize{640,360},CCSize{960,540},CCSize{320,240},CCSize{400,300}}){
 CCNode menu,clock;menu.add(&clock);clock.size={124,28};assert(typeinfo_cast<CCMenuItem*>(static_cast<CCNode*>(&clock))==nullptr);
 CCMenuItem left;left.size={145,win.height*.42f};left.pos={0,win.height*.58f};left.enabled=false;menu.add(&left);
 placeSurvivalMenuClock(&menu,&clock,win);assert(clock.visible);auto area=menuWorldBounds(&clock,{0,0,124,28});assert(!buttonOverlaps(&menu,&clock,area));assert(clock.pos.x>145);
 CCMenuItem rotated;rotated.size={60,10};rotated.pos={250,200};rotated.angle=1.5707963f;rotated.sx=-1;menu.add(&rotated);
 assert(buttonOverlaps(&menu,&clock,{242,145,12,20}));rotated.visible=false;assert(!buttonOverlaps(&rotated,&clock,{242,145,12,20}));
 CCMenuItem full;full.size=win;menu.add(&full);placeSurvivalMenuClock(&menu,&clock,win);assert(!clock.visible);
 full.visible=false;left.visible=false;placeSurvivalMenuClock(&menu,&clock,win);assert(clock.visible&&clock.pos.x==9);
 menu.angle=.2f;menu.sx=-1;placeSurvivalMenuClock(&menu,&clock,win);assert(clock.visible);
 }}
 `);
});

test('audio errors follow pinned native codes without inventing permanent/storage failure causes',()=>run(`
#include "domain/RoundAudioFailure.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){for(auto kind:{RoundResourceKind::SongInfo,RoundResourceKind::Song,RoundResourceKind::Sfx}){
 assert(classifyRoundAudioFailure(kind,2)==RoundAudioCategory::Permanent);
 assert(classifyRoundAudioFailure(kind,3)==RoundAudioCategory::Transient);
 assert(classifyRoundAudioFailure(kind,0)==RoundAudioCategory::Unknown);
 assert(classifyRoundAudioFailure(kind,1)!=RoundAudioCategory::Permanent);
 assert(classifyRoundAudioFailure(kind,1)!=RoundAudioCategory::Storage);
 assert(classifyRoundAudioFailure(kind,0,true)==RoundAudioCategory::Cache);
 }assert(classifyRoundAudioFailure(RoundResourceKind::SongInfo,1)==RoundAudioCategory::Metadata);}
`));
