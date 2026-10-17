// Compile production shared components with Cocos API doubles, not a device renderer.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=path=>readFileSync(join(root,path),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end);}
function run(cpp){const dir=mkdtempSync(join(tmpdir(),'corum-survival-ui-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('Solo and Survival shared HUD fonts retain native fallback, shadows, width and opacity',()=>{
    const visuals=read('src/ui/RankedVisuals.hpp');
    run(`
#include "domain/FontMetrics.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCPoint{float x,y;};struct ccColor3B{unsigned char r,g,b;};
struct CCNode{virtual ~CCNode()=default;std::vector<CCNode*> children;void addChild(CCNode* n,int){children.push_back(n);}};
struct CCLabelBMFont:CCNode {
 std::string value,resource;float scale=1;CCPoint position{},anchor{};ccColor3B color{};GLubyte alpha=255;inline static bool fallback=false;
 static CCLabelBMFont* create(char const* t,char const* f){if(fallback&&std::string(f).find("CorumSans")!=std::string::npos)return nullptr;auto* n=new CCLabelBMFont;n->value=t;n->resource=f;return n;}
 char const* getFntFile()const{return resource.c_str();}float getScale()const{return scale;}
 void setString(char const* s){value=s;}void setScale(float s){scale=s;}void setColor(ccColor3B c){color=c;}void setPosition(CCPoint p){position=p;}void setAnchorPoint(CCPoint p){anchor=p;}void setOpacity(GLubyte a){alpha=a;}
 void limitLabelWidth(float w,float maximum,float minimum){scale=std::clamp(w/(resource.find("CorumSans")!=std::string::npos?480.f:240.f),minimum,maximum);}
};
}
using namespace cocos2d;
constexpr char const* operator""_spr(char const* s,std::size_t){return s;}
namespace corum::ranked::ui {
enum class FontWeight{Regular,Medium,SemiBold,ExtraBold,Black};
${['inline char const* font(','inline float fontScale(','inline void setLabelScale(','inline void fitLabelWidth(','inline CCLabelBMFont* playerNameLabel(','inline CCLabelBMFont* label('].map(n=>method(visuals,n)).join('\n')}
}
${read('src/ui/RankedHudLabel.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'')}
int main(){using namespace corum::ranked::ui;
 CCNode root;auto h=rankedHudLabel(&root,"03:00",FontWeight::Black,1.02f,{.5f,1},{250,300},{255,255,255});
 assert(root.children.size()==2&&h.foreground->resource=="CorumSansBlack.fnt");
 assert(std::abs(h.foreground->scale-.51f)<.001f&&h.shadow->scale==h.foreground->scale);
 assert(h.shadow->alpha==205&&h.shadow->position.x==250.75f&&h.shadow->position.y==299.25f);
 h.text("02:59");assert(h.foreground->value=="02:59"&&h.shadow->value==h.foreground->value);
 h.fit(70,.36f,.20f);assert(h.foreground->scale==h.shadow->scale);h.opacity(128);assert(h.foreground->alpha==128&&h.shadow->alpha==102);
 CCLabelBMFont::fallback=true;auto fallback=rankedHudLabel(&root,"한글",FontWeight::Black,.36f,{0,1},{8,270},{255,255,255});
 assert(fallback.foreground->resource=="goldFont.fnt"&&fallback.foreground->scale==.36f);
 for(auto const* name:{"HwanHee","hwanhee","HWANHEE"}){
  auto n=rankedHudPlayerName(&root,name,FontWeight::Black,.36f,{0,1},{8,270},{255,255,255});
  assert(n.foreground->resource=="chatFont.fnt"&&n.foreground->value==name&&n.shadow->value==name);
 }
 for(auto* child:root.children)delete child;
}`);
});

test('chooser tap shine follows the same clockwise perimeter once and continues after hover input closes',()=>{
    const source=read('src/ui/LobbyHover.hpp');
    const shine=source.slice(source.indexOf('inline CCPoint lobbyCardPerimeter('),source.indexOf('// Owned by this lobby render'));
    run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>
struct CCPoint{float x,y;};struct Color{float r,g,b,a;};
struct CCAction;struct CCNode{virtual ~CCNode()=default;bool removed=false;std::vector<CCNode*> children;CCAction* action=nullptr;void addChild(CCNode* c,int){children.push_back(c);}void runAction(CCAction* a){action=a;}void removeFromParentAndCleanup(bool){removed=true;}};
struct CCAction{virtual ~CCAction()=default;virtual void autorelease(){}virtual void startWithTarget(CCNode*){}virtual void update(float){}};
struct CCActionInterval:CCAction{float duration=0;bool initWithDuration(float d){duration=d;return true;}};
struct CCDrawNode:CCNode{std::vector<CCPoint> segments;static CCDrawNode* create(){return new CCDrawNode;}void clear(){segments.clear();}void drawSegment(CCPoint a,CCPoint,float,Color){segments.push_back(a);}};
struct CallFuncExt:CCAction{std::function<void()> fn;static CallFuncExt* create(std::function<void()> fn){auto* a=new CallFuncExt;a->fn=std::move(fn);return a;}void finish(){fn();}};
struct CCSequence:CCAction{CCAction* animation;CallFuncExt* done;static CCSequence* create(CCAction* a,CallFuncExt* d,std::nullptr_t){auto* s=new CCSequence;s->animation=a;s->done=d;return s;}};
struct CCMenuItemSpriteExtra{};
template<class T>struct Ref{T* p;T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* p):p(p){}Ref<T> lock()const{return {p};}};
${shine}
struct Controller {
 struct Target{WeakRef<CCMenuItemSpriteExtra> item;WeakRef<CCNode> node;bool card=true,inside=false;CCDrawNode* shine=nullptr;float shineTime=1;};
 bool m_active=true,m_cardsSelectable=true;std::vector<Target> m_targets;int pulses=0;void pulse(Target&,bool){++pulses;}
 ${method(source,'void press(CCMenuItemSpriteExtra* item)')}
};
int main(){
 auto top=lobbyCardPerimeter(0),right=lobbyCardPerimeter(188),bottom=lobbyCardPerimeter(398),left=lobbyCardPerimeter(586);
 assert(top.x==-94&&top.y==105&&right.x==94&&right.y==105&&bottom.x==94&&bottom.y==-105&&left.x==-94&&left.y==-105);
 CCNode card;CCMenuItemSpriteExtra item;Controller hover;hover.m_targets.push_back({WeakRef(&item),WeakRef(&card)});
 hover.press(&item);assert(card.children.size()==1&&hover.pulses==1);
 auto* shine=dynamic_cast<CCDrawNode*>(card.children[0]);auto* seq=dynamic_cast<CCSequence*>(shine->action);assert(shine&&seq);
 hover.m_active=false;seq->animation->startWithTarget(shine);seq->animation->update(.5f);assert(shine->segments.size()==22);
 seq->animation->update(1);assert(shine->segments.empty());seq->done->finish();assert(shine->removed);
 hover.press(&item);assert(card.children.size()==1);hover.m_active=true;hover.m_cardsSelectable=false;hover.press(&item);assert(card.children.size()==1);
 delete seq->animation;delete seq->done;delete seq;delete shine;
}`);
});

test('Survival keeps surrender in native Pause and uses the shared clock pair with compact unbacked rows',()=>{
    const play=read('src/PlayLayer.cpp'),hud=read('src/ui/SurvivalHud.cpp'),pause=read('src/RankedPauseLayer.cpp');
    assert.doesNotMatch(method(play,'void addSurvivalHud()'),/CCMenuItem|surrender|Force n/);
    assert.match(pause,/menu_selector\(CorumRankedPauseLayer::onSurrender\)/);
    assert.match(pause,/GJ_closeBtn_001\.png/);
    assert.match(hud,/FontWeight::ExtraBold,CompetitiveClockScale/);
    assert.match(hud,/competitiveClockPair\(/);
    assert.doesNotMatch(hud,/CCLayerColor::create|From Cutoff|m_differenceCaption/);
    assert.match(hud,/row\.name\.position\(\{rankWidth\+4/);
    assert.match(hud,/CCEaseBackOut::create/);
    const roster=read('src/ui/SurvivalGameLayer.cpp');
    assert.match(roster,/createAnimatedRankBadge\(value\.tier/);
    assert.match(roster,/RankBadgeDisplay::Leaderboard/);
    assert.match(roster,/milliseconds\(SurvivalRosterMilliseconds\)/);
});


test('deferred Survival launch rechecks round, scene and confirmation and never constructs inside update',()=>{
    const launch=method(read('src/ui/SurvivalGameLayer.cpp'),'    void queueNativeLaunch()');
    run(`
#include <cassert>
#include <functional>
#include <string>
#include <vector>
#include <utility>
#include <optional>
#include "${join(root,'src/domain/LevelPreparationPolicy.hpp')}"
using corum::ranked::LevelPreparationContext;
struct CCScene{};
struct CCDirector{CCScene* scene=nullptr;static CCDirector* sharedDirector(){static CCDirector d;return &d;}CCScene* getRunningScene(){return scene;}};
std::vector<std::function<void()>> work;
void queueInMainThread(std::function<void()> f){work.push_back(std::move(f));}
void drain(){auto queued=std::exchange(work,{});for(auto& f:queued)f();}
std::string preparationContextText(LevelPreparationContext const& c){return c.matchId+":"+std::to_string(c.levelId)+":"+std::to_string(c.round);}
bool inUpdate=false;
struct RankedRoundPreparation{int launches=0;bool success=true;static RankedRoundPreparation& get(){static RankedRoundPreparation p;return p;}bool launch(CCScene*){assert(!inUpdate);++launches;return success;}};
struct SurvivalRuntime{std::int64_t sync=1;std::int64_t serverSyncMillis(){return sync;}bool enter=true;LevelPreparationContext context{"match",40000000,1,0,false,0,0};static SurvivalRuntime& get(){static SurvivalRuntime r;return r;}auto preparationContext(){return context;}bool canEnter(){return enter;}};
template<class T>struct WeakRef{T* pointer;explicit WeakRef(T* p):pointer(p){}struct Locked{T* ptr;explicit operator bool()const{return ptr&&ptr->alive;}T* operator->(){return ptr;}T* data(){return ptr;}};auto lock()const{return Locked{pointer};}};
struct Dialog{bool alive=true;CCScene* parent=nullptr;CCScene* getParent(){return parent;}};
struct Coordinator{std::optional<WeakRef<Dialog>> m_jukebox;bool alive=true,running=true,m_closed=false,m_motion=false,m_confirmation=false,m_browsingBoard=false;std::optional<std::int64_t> m_entrySyncAfter;std::string m_pendingLaunch,m_launchedContext;CCScene* parent=nullptr;bool isRunning(){return running;}CCScene* getParent(){return parent;}
${launch}
};
int main(){CCScene origin,other;auto* director=CCDirector::sharedDirector();director->scene=&origin;
 auto& runtime=SurvivalRuntime::get();auto& preparation=RankedRoundPreparation::get();Coordinator owner;owner.parent=&origin;
 inUpdate=true;owner.queueNativeLaunch();owner.queueNativeLaunch();assert(work.size()==1&&preparation.launches==0);inUpdate=false;
 drain();assert(preparation.launches==1&&!owner.m_launchedContext.empty()&&owner.m_pendingLaunch.empty());
 for(int changed=0;changed<9;++changed){owner.m_launchedContext.clear();owner.queueNativeLaunch();
  if(changed==0){++runtime.context.round;}
  if(changed==1){director->scene=&other;}
  if(changed==2){owner.m_closed=true;}
  if(changed==3){owner.m_confirmation=true;}
  if(changed==4){owner.running=false;}
  if(changed==5){runtime.enter=false;}
  Dialog dialog;dialog.parent=&origin;if(changed==6)owner.m_jukebox=WeakRef(&dialog);
  if(changed==7)owner.m_browsingBoard=true;
  if(changed==8)owner.m_entrySyncAfter=runtime.sync;
  drain();assert(preparation.launches==1&&owner.m_launchedContext.empty()&&owner.m_pendingLaunch.empty());
  owner.m_jukebox.reset();owner.m_entrySyncAfter.reset();owner.m_browsingBoard=false;owner.m_closed=false;owner.m_confirmation=false;owner.running=true;runtime.enter=true;director->scene=&origin;
 }
 preparation.success=false;owner.queueNativeLaunch();drain();assert(preparation.launches==2&&owner.m_launchedContext.empty());
 preparation.success=true;owner.queueNativeLaunch();drain();assert(preparation.launches==3&&!owner.m_launchedContext.empty());
 owner.queueNativeLaunch();owner.alive=false;drain();assert(preparation.launches==3);
}`);
});

test('actual Survival progress markers follow server records and native transforms without enabling a hidden bar',()=>{
    const source=read('src/ui/SurvivalHud.cpp');
    run(`
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
#include <vector>
#include "${join(root,'src/domain/SurvivalHudPresentation.hpp')}"
struct CCPoint{float x=0,y=0;};struct ccColor4F{float r,g,b,a;};
struct GameManager{struct Color{unsigned char r=10,g=120,b=230;};static GameManager*sharedState(){static GameManager m;return &m;}int getPlayerColor(){return 0;}Color colorForIdx(int){return {};}};
constexpr char const* operator""_spr(char const* s,std::size_t){return s;}
struct CCNode{bool alive=true,visible=true;void setVisible(bool v){visible=v;}bool isVisible(){return visible;}};
struct CCDrawNode:CCNode{struct Segment{CCPoint a,b;float radius;ccColor4F tint;};std::vector<Segment> segments;
 static CCDrawNode* create(){return new CCDrawNode;}void setID(char const*){}void clear(){segments.clear();}void drawSegment(CCPoint a,CCPoint b,float r,ccColor4F c){segments.push_back({a,b,r,c});}};
struct CCSprite:CCNode{CCPoint anchor{0,.5f},size{58,8},world{0,0};float scale=2;std::vector<CCDrawNode*> children;
 void addChild(CCDrawNode* n,int){children.push_back(n);}auto getAnchorPoint(){return anchor;}
 CCPoint getAnchorPointInPoints(){return {anchor.x*size.x,anchor.y*size.y};}
 CCPoint convertToWorldSpace(CCPoint p){auto a=getAnchorPointInPoints();return {world.x+(p.x-a.x)*scale,world.y+(p.y-a.y)*scale};}
 CCPoint convertToNodeSpace(CCPoint p){auto a=getAnchorPointInPoints();return {(p.x-world.x)/scale+a.x,(p.y-world.y)/scale+a.y};}
};
template<class T>struct WeakRef{T* pointer=nullptr;WeakRef()=default;explicit WeakRef(T* p):pointer(p){}struct Locked{T* ptr;explicit operator bool()const{return ptr&&ptr->alive;}T* operator->(){return ptr;}T* data(){return ptr;}};auto lock()const{return Locked{pointer};}};
namespace corum::ranked {struct SurvivalMatchView{std::string ownId;std::optional<double> cutoff;std::vector<SurvivalStanding> standings;};}
namespace corum::ranked::ui {struct SurvivalHud{WeakRef<CCSprite> m_progressBar,m_progressFill;WeakRef<CCDrawNode> m_progressLines;float m_progressWidth=0,m_progressHeight=0;SurvivalScoreTween m_personalBest;
 void attachProgressBar(CCSprite*,CCSprite*,float,float);void presentProgressMarkers(SurvivalMatchView const&);};
${method(source,'void SurvivalHud::attachProgressBar(')}
${method(source,'void SurvivalHud::presentProgressMarkers(')}
}
int main(){using namespace corum::ranked;using corum::ranked::ui::SurvivalHud;
 SurvivalHud hud;SurvivalMatchView view;view.ownId="winner";view.cutoff=75.5;view.standings.push_back({"winner","Bot",1,0,0,93.2,true,true,false});
 hud.attachProgressBar(nullptr,nullptr,200,8);hud.presentProgressMarkers(view);assert(!hud.m_progressLines.lock());
 CCSprite bar,fill;bar.anchor={0,0};bar.size={210,12};bar.world={100,200};fill.world={110,212};
 hud.attachProgressBar(&bar,&fill,200,8);hud.presentProgressMarkers(view);
 auto* lines=bar.children.at(0);assert(lines->segments.size()==7&&lines->visible);
 assert(std::abs(lines->segments[0].a.x-156)<.001f&&std::abs(lines->segments[2].a.x-191.4f)<.001f);
 assert(lines->segments[0].tint.r==1&&lines->segments[0].tint.g<.4f&&lines->segments[2].tint.g>.8f);
 fill.size.x=198;hud.presentProgressMarkers(view);assert(std::abs(lines->segments[0].a.x-156)<.001f);
 bar.visible=false;hud.presentProgressMarkers(view);assert(!bar.visible&&!lines->visible);
 bar.visible=true;fill.visible=false;hud.presentProgressMarkers(view);assert(!fill.visible&&!lines->visible);
 fill.visible=true;view.cutoff=0;view.standings[0].score=100;hud.presentProgressMarkers(view);
 assert(lines->segments[0].a.x==5&&lines->segments[2].a.x==205);
 hud.m_personalBest.observe(100);for(int i=0;i<9;i++)hud.m_personalBest.advance(.25);hud.presentProgressMarkers(view);
 assert(lines->segments[5].a.x==205);
 assert(std::abs(lines->segments[5].tint.r-10.f/255)<.001f);
 view.cutoff=100;hud.presentProgressMarkers(view);
 assert(lines->segments[0].a.x==lines->segments[2].a.x&&lines->segments[2].a.x==lines->segments[5].a.x);
 assert(lines->segments[1].a.y!=lines->segments[3].a.y&&lines->segments[3].a.y!=lines->segments[6].a.y);
 view.cutoff.reset();view.standings.clear();hud.presentProgressMarkers(view);assert(lines->segments.empty());
 fill.alive=false;hud.presentProgressMarkers(view);assert(!lines->visible&&lines->segments.empty());
 delete lines;
}`);
});

test('actual Survival audio policy keeps survivor music, uses Final preparation, and only restarts on mode changes',()=>{
 const sync=method(read('src/ui/SurvivalGameLayer.cpp'),'    void syncAudio(');
 const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
 const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
 run(`
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <map>
#include <variant>
#include <vector>
#include "domain/SurvivalRoundMenuPolicy.hpp"
using namespace corum::ranked;
${json}
struct SurvivalMatchView{matjson::Value result,completedRound;std::string phase,state="MAP_SELECTION",roundKey="r1",ownId="human";bool ownWinner=false,ownEliminated=false;};
enum class RankedAudioMode{Round12,Round3,Round3DeathmatchWait,ResultWin,ResultLose};
struct RankedAudioManager{int changes=0,ticks=0;RankedAudioMode mode=RankedAudioMode::Round3;static auto& get(){static RankedAudioManager a;return a;}void setMode(RankedAudioMode m){mode=m;changes++;}void tick(){ticks++;}};
struct Coordinator{std::optional<RankedAudioMode> m_audioMode;
${sync}
};
int main(){Coordinator layer;auto& audio=RankedAudioManager::get();SurvivalMatchView view;
 for(auto const* phase:{"ROUND_1","ROUND_2","ROUND_3"}){view.phase=phase;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::Round12);}
 assert(audio.changes==1&&audio.ticks==3);
 for(auto const* phase:{"FINAL","TIEBREAK"}){view.phase=phase;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::Round3DeathmatchWait);}
 layer.syncAudio(view,true);assert(audio.mode==RankedAudioMode::Round3DeathmatchWait);auto changes=audio.changes;
 layer.syncAudio(view,true);assert(audio.changes==changes);
 layer.syncAudio(view);assert(audio.mode==RankedAudioMode::Round3DeathmatchWait);
 view.ownEliminated=true;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::ResultLose);
 view.result["rank"]=2;view.result["won"]=false;view.ownEliminated=false;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::ResultLose);
 view.result["rank"]=1;view.result["won"]=true;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::ResultWin);
 changes=audio.changes;layer.syncAudio(view);assert(audio.changes==changes);
 view.result["rank"]=1;view.result["won"]=false;layer.syncAudio(view);assert(audio.mode==RankedAudioMode::ResultLose);
 changes=audio.changes;layer.m_audioMode.reset();layer.syncAudio(view);assert(audio.changes==changes+1);
}
`);
});
