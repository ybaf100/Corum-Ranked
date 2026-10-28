import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,'src',p),'utf8');
function method(s,key){const a=s.indexOf(key);assert(a>=0,key);const b=s.indexOf('{',a);let n=1,e=b+1;while(n){if(s[e]==='{')n++;if(s[e]==='}')n--;e++;assert(e<=s.length);}return s.slice(a,e);}
function run(cpp){const d=mkdtempSync(join(tmpdir(),'corum-evidence-'));try{writeFileSync(join(d,'t.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(d,'t.cpp'),'-o',join(d,'t')],{stdio:'pipe'});execFileSync(join(d,'t'),[],{stdio:'pipe'});}catch(e){throw Error(e.stderr?.toString()||e.message);}finally{rmSync(d,{recursive:true,force:true});}}

test('production selected-panel hold preserves pre-start five seconds but never restarts a PLAYING round',()=>{
 const body=method(read('SurvivalRuntime.cpp'),'std::optional<std::int64_t> SurvivalRuntime::selectedPanelWaitMs()').replace('SurvivalRuntime::','');
 run(`#include "domain/SurvivalEntryHold.hpp"
#include <cassert>
#include <chrono>
#include <string>
using namespace corum::ranked;
std::optional<long> parseIso8601Millis(std::string const& s){return s.empty()?std::nullopt:std::optional(std::stol(s));}
struct Runtime{struct View{std::string state="ENTERING",entryRequestedAt="1000",entryOpensAt="6000";bool ownAssetsReady=true;}m_view;
 struct Clock{bool sync=true;bool synchronized()const{return sync;}long serverNowMillis(long)const{return 6000;}}m_clock;
 mutable SurvivalEntryHold m_entryHold;std::string m_selectedPanelContext="round";bool entered=false;
 auto optionsContext()const{return std::string("round");}long localNow()const{return 0;}bool previouslyEntered()const{return entered;}
 std::optional<std::int64_t> entryWaitMs()const{return -10;}
 ${body}
};
int main(){Runtime r;auto wait=r.selectedPanelWaitMs();assert(wait&&*wait>4900);r.m_selectedPanelContext="board";assert(!r.selectedPanelWaitMs());
 for(bool entered:{false,true}){r.entered=entered;r.m_view.state="PLAYING";assert(r.selectedPanelWaitMs()==0);}
 r.m_view.state="COUNTDOWN";r.entered=false;r.m_selectedPanelContext="round";assert(r.selectedPanelWaitMs().value()>0);
}`);
});

test('production live entry button remains visible while preparing, then enables without prior native entry',()=>{
 const src=read('ui/SurvivalSelectedMapPanel.cpp');const a=src.indexOf('    auto const retry=!prep.error.empty();'),b=src.indexOf('    auto const wait=',a);assert(a>=0&&b>a);
 run(`#include <cassert>
#include <string>
struct SurvivalRuntime{bool entered=false,observer=false;static SurvivalRuntime&get(){static SurvivalRuntime r;return r;}bool previouslyEntered(){return entered;}bool observing(){return observer;}};
struct Button{bool visible=false,enabled=false;void setNormalImage(int){}void setTag(int){}void setVisible(bool v){visible=v;}void setEnabled(bool v){enabled=v;}};
struct Panel{Button button,countdown;Button*m_ready=&button,*m_countdown=&countdown;bool m_readyEnabled=false,m_retry=false,m_start=false;int readyFace(bool,bool,bool){return 0;}
 void present(std::string state,bool canEnter){struct View{std::string state;}view{state};struct Prep{std::string error;}prep;bool interactive=true;
 ${src.slice(a,b)}
 }};
int main(){Panel p;p.present("ENTERING",false);assert(!p.button.visible&&p.countdown.visible);
 p.present("PLAYING",false);assert(p.button.visible&&!p.button.enabled&&!p.countdown.visible);
 p.present("PLAYING",true);assert(p.button.visible&&p.button.enabled&&!p.countdown.visible);
 SurvivalRuntime::get().observer=true;p.present("PLAYING",true);assert(p.button.enabled);
}`);
});

test('actual overlay slots never run Cocos weak destructors at process shutdown; normal exit releases only its own overlays',()=>{
 const end=read('EndLevelLayer.cpp'),pause=read('RankedPauseLayer.cpp');
 const decl=[...end.matchAll(/^auto& (?:ranked|survival)CompletionOverlay = .*;$/gm),...pause.matchAll(/^auto& activeRankedPause = .*;$/gm)].map(m=>m[0]).join('\n');assert.equal(decl.split('\n').length,3);
 const exit=method(end,'void onExit()');
 run(`#include <cassert>
#include <cstdlib>
#include <optional>
#include <cstddef>
bool engineStopped=false;
struct EndLevelLayer{int exits=0;void onExit(){++exits;}};struct PauseLayer{};
template<class T>struct Ref{T*p;explicit Ref(T*v):p(v){}T*data(){return p;}T*operator->(){return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T*p=nullptr;WeakRef()=default;explicit WeakRef(T*v):p(v){}~WeakRef(){if(engineStopped)std::abort();}auto lock(){return Ref<T>(p);}};
${decl}
#define schedule_selector(...) 0
struct Overlay:EndLevelLayer{int unscheduled=0;void unschedule(int){++unscheduled;}${exit}};
int main(){for(int round=0;round<100;++round){Overlay old,current;rankedCompletionOverlay=WeakRef<EndLevelLayer>(&current);survivalCompletionOverlay=WeakRef<EndLevelLayer>(&current);
 old.onExit();assert(rankedCompletionOverlay.lock().data()==&current);current.onExit();assert(!rankedCompletionOverlay.lock()&&!survivalCompletionOverlay.lock());assert(current.exits==1&&current.unscheduled==2);}
 // Simulate the user exiting while Pause/completion is still open. Engine is already gone
 // when the DLL exit table runs: process-lived slots deliberately do not destruct then.
 static Overlay stillOpen;rankedCompletionOverlay=WeakRef<EndLevelLayer>(&stillOpen);static PauseLayer p;activeRankedPause.emplace(&p);engineStopped=true;
}`);
});

test('actual fancy mode predicate covers four combinations while selected-card thumbnails stay independent and numbers still animate',()=>{
 const src=read('ui/SurvivalMapSelectionLayer.cpp');const expr=src.match(/m_thumbnail=record.mode.choose\(([^;]+)\);/);assert(expr);
 const panel=src.match(/m_panel->present\(size,([^,]+),view/);assert(panel);
 assert.doesNotMatch(src,/if\(context.levelId && !SurvivalOptionsPreferences::get\(\).selectionMotion\(\)\)m_timeline.skip\(\)/);
 run(`#include "domain/SurvivalMapSelection.hpp"
#include <cassert>
using namespace corum::ranked;
struct SurvivalOptionsPreferences{bool fancy=true;static auto&get(){static SurvivalOptionsPreferences p;return p;}bool selectionMotion(){return fancy;}};
int main(){for(bool supported:{false,true})for(bool fancy:{false,true}){SurvivalOptionsPreferences::get().fancy=fancy;
 struct Record{SurvivalRevealModeChoice mode;}record;bool previewsReady=true;bool m_thumbnail=record.mode.choose(${expr[1]});
 bool panelThumbnail=${panel[1]};assert(m_thumbnail==(supported&&fancy));assert(panelThumbnail==supported);
 SurvivalMapSelectionTimeline timeline;timeline.observe({"match","R1",1,123456789,1},1000);assert(!timeline.frame(2000).settled);assert(timeline.frame(5000).settled);
 timeline.followServer("PLAYING",{},2000);assert(timeline.frame(2000).settled);
 }}
`);
});

test('production numeric-mode selected thumbnail paints the real cached image once without building a carousel',()=>{
 const body=method(read('ui/SurvivalMapSelectionLayer.cpp'),'void SurvivalMapSelectionLayer::selectedThumbnailFrame(').replace('SurvivalMapSelectionLayer::','');
 assert.match(read('ui/SurvivalMapSelectionLayer.cpp'),/if\(supported\)selectedThumbnailFrame\(frame,r\)/);
 run(`#include "domain/SurvivalMapSelection.hpp"
#include "domain/SurvivalSelectionLayout.hpp"
#include <cassert>
using namespace corum::ranked;
using GLubyte=unsigned char;
struct Node{float x=0,y=0,scale=0;int opacity=0;bool visible=false;void setZOrder(int){}void setPosition(std::pair<float,float>p){x=p.first;y=p.second;}void setScale(float f){scale=f;}void setVisible(bool v){visible=v;}void setOpacity(int p){opacity=p;}};
struct Texture{};
struct Ref{Texture*p=nullptr;Texture*data(){return p;}explicit operator bool()const{return p;}};
struct SurvivalThumbnailProvider{Ref image;static auto&get(){static SurvivalThumbnailProvider p;return p;}auto selected(){return image;}};
void setModeLobbyOpacity(Node*n,GLubyte p){n->opacity=p;}
struct Layer{struct Card{Node node,shadeNode,trimNode;Node*root=nullptr,*shade=&shadeNode,*trim=&trimNode;float width=0;Texture*texture=nullptr;int builds=0;
 void make(Layer*,float w){width=w;root=&node;++builds;}void set(Texture*t){texture=t;}};
 struct Visuals{Card selected;Ref finalTexture;}visuals;Visuals*m_visuals=&visuals;
 ${body}
};
int main(){Layer layer;auto r=survivalSelectionLayout(568,426,true,true);SurvivalRevealFrame f;f.details=0;
 layer.selectedThumbnailFrame(f,r);assert(!layer.visuals.selected.root->visible);Texture real;SurvivalThumbnailProvider::get().image={&real};
 f.details=.5;layer.selectedThumbnailFrame(f,r);auto&card=layer.visuals.selected;assert(card.texture==&real&&card.root->visible&&card.root->opacity==127);
 f.details=1;for(int i=0;i<100;++i)layer.selectedThumbnailFrame(f,r);assert(card.builds==1&&card.root->opacity==255&&card.root->x==r.thumbnail.cx()&&card.root->y==r.thumbnail.cy());
}`);
});

test('production board/back actions preserve initial entry before start and expose manual entry after start',()=>{
 const source=read('ui/SurvivalGameLayer.cpp');const actions=source.split('\n').filter(l=>l.includes('if(tag==8)')||l.includes('if(tag==9)')).join('\n');assert(!actions.includes('m_manualMap=true'));assert(!actions.includes('++m_boardVisit'));
 run(`#include <cassert>
#include <optional>
#include <string>
#include <cstdint>
struct Runtime{struct View{std::string state="ENTERING";}v;bool entered=false;int refreshes=0;auto const&view(){return v;}bool previouslyEntered(){return entered;}long serverSyncMillis(){return 42;}void refreshRound(){++refreshes;}};
struct Board{bool m_browsingBoard=false,m_manualMap=false;std::uint64_t m_boardVisit=0;std::optional<long>m_entrySyncAfter;
 void action(int tag,Runtime& runtime){${actions}}
};
int main(){for(auto state:{"ENTERING","COUNTDOWN","PLAYING"})for(bool entered:{false,true}){Runtime runtime;runtime.v.state=state;runtime.entered=entered;Board b;
 b.action(9,runtime);assert(b.m_browsingBoard&&!b.m_manualMap&&b.m_boardVisit==0&&runtime.entered==entered);
 b.action(8,runtime);assert(!b.m_browsingBoard&&!b.m_manualMap&&b.m_boardVisit==0);assert(runtime.refreshes==1&&!b.m_entrySyncAfter);
}}
`);
});
