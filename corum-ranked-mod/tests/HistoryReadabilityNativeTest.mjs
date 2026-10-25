import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
function method(source,name){let a=source.indexOf(name);assert(a>=0,name);let b=source.indexOf('{',a)+1,n=1;while(n){n+=(source[b]==='{')-(source[b]==='}');++b;}return source.slice(a,b);}
function run(source){const dir=mkdtempSync(join(tmpdir(),'history-readability-'));try{writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('actual history cards distinguish confirmed elimination from non-participation and retain red when selected',()=>{
 const source=read('src/RankedPopup.cpp');
 run(`#include <cassert>
#include <optional>
#include <string>
#include <vector>
#include "domain/SurvivalHudPresentation.hpp"
struct ccColor3B{int r=0,g=0,b=0;bool operator==(ccColor3B const&)const=default;};
struct ccColor4B{int r,g,b,a;};
ccColor3B ccc3(int r,int g,int b){return {r,g,b};}
constexpr ccColor3B kGold{255,200,1},kCyan{1,200,255},kRed{255,91,111},kGreen{1,255,150},kBlue{1,50,255},kDeepPanel2{10,19,36};
namespace corum::ranked{struct HistoryRound{bool completed=false;std::optional<bool>ownQualified;std::optional<int>ownRank={};};
namespace ui{constexpr ccColor3B Text{242,247,255},TextMuted{164,181,205};}}
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};constexpr CCPoint CCPointZero{};
struct CCNode{ccColor3B tint;CCPoint position;CCSize size;int tag=0;std::vector<CCNode*>children;
 virtual ~CCNode(){for(auto*c:children)delete c;}void addChild(CCNode*n,int=0){children.push_back(n);}
 void setAnchorPoint(CCPoint){}void setPosition(CCPoint p){position=p;}void setContentSize(CCSize s){size=s;}void setTag(int n){tag=n;}};
struct CCLayerColor:CCNode{static CCLayerColor*create(ccColor4B c){auto*p=new CCLayerColor;p->tint={c.r,c.g,c.b};return p;}};
struct CCMenu:CCNode{static CCMenu*create(){return new CCMenu;}};
using SEL_MenuHandler=int;
struct CCMenuItemSpriteExtra:CCNode{static CCMenuItemSpriteExtra*create(CCNode*n,void*,SEL_MenuHandler){auto*p=new CCMenuItemSpriteExtra;p->addChild(n);return p;}};
CCNode*makeNeonPanel(CCSize s,CCPoint p,ccColor3B c,ccColor3B,int){auto*n=new CCNode;n->size=s;n->position=p;n->tint=c;return n;}
struct Layer{
 void historyText(CCNode*p,std::string const&,float,CCPoint,float,ccColor3B c){auto*l=new CCNode;l->tint=c;p->addChild(l);}
 ${method(source,'    ccColor3B historyRoundColor(')}
 ${method(source,'    CCMenuItemSpriteExtra* historyButton(')}
};
int main(){Layer layer;CCNode parent;
 for(auto state:{std::optional<bool>{},std::optional<bool>{false},std::optional<bool>{true}}){
  corum::ranked::HistoryRound round{false,state};assert(layer.historyRoundColor(round)==kGold);
  round.completed=true;auto color=layer.historyRoundColor(round);
  assert(color==(state.has_value()?(*state?kGreen:kRed):corum::ranked::ui::TextMuted));
  for(bool selected:{false,true}){
   auto*button=layer.historyButton(&parent,"R2",{50,40},49,0,2,selected,color);
   auto*face=button->children.at(0);assert(button->tag==2&&face->tint==color);
   assert(face->children.at(0)->tint==(selected?kCyan:corum::ranked::ui::Text));
   assert(face->children.size()==1);
  }
 }
 for(int rank=1;rank<=6;++rank){corum::ranked::HistoryRound r{true,true,rank};auto c=corum::ranked::survivalRankColor(rank,true);assert(layer.historyRoundColor(r)==ccc3(c.r,c.g,c.b));r.ownQualified=false;assert(layer.historyRoundColor(r)==kRed);}
 auto silver=corum::ranked::survivalRankColor(2,true);assert(silver.r>=235&&silver.g>=240&&silver.b>=250);
 for(bool selected:{false,true}){
  auto*button=layer.historyButton(&parent,"SOLO",{50,40},49,0,0,selected);auto*face=button->children.at(0);
  assert(face->tint==(selected?kCyan:kBlue));assert(face->children.size()==1);
 }
}
`);
 assert.match(method(source,'    void renderHistoryList('),/auto const tint=historyRoundColor\(round\)/);
 assert.match(method(source,'    void renderHistoryDetail('),/survival\?std::optional<ccColor3B>\{historyRoundColor\(rounds\[i\]\)\}:std::nullopt/);
 assert.match(method(source,'    void historyOwnPanel('),/position,historyRoundColor\(round\)/);
});

test('actual single-line fitter shrinks long names, clears stale wrap, preserves casing and restores short-name size',()=>run(`
#include "domain/FontMetrics.hpp"
#include <cassert>
#include <cmath>
#include <string>
using namespace corum::ranked;
struct Size{float width,height;};
struct CCLabelBMFont{std::string name="hwanhee1234567890123456789012345";float width=18,scale=1;bool wraps=true;
 void setWidth(float w){width=w;}void setLineBreakWithoutSpace(bool w){wraps=w;}
 void setScale(float s){scale=s;}float getScale()const{return scale;}Size getContentSize()const{return {static_cast<float>(name.size()*32),64};}};
void setLabelScale(CCLabelBMFont* p,float s){p->setScale(corumFontScale(s,"CorumSansBlack.fnt"));}
${method(read('src/ui/RankedVisuals.hpp'),'inline void fitSingleLineMenuLabel(')}
int main(){CCLabelBMFont text;auto original=text.name;for(float width:{16.f,30.f,52.f,102.f}){
 text.width=18;text.wraps=true;fitSingleLineMenuLabel(&text,width,12,.25f);
 assert(text.width==-1&&!text.wraps&&text.name==original);assert(text.getContentSize().width*text.scale<=width+.001f);assert(64*text.scale<=12+.001f);
}auto small=text.scale;text.name="Bot 1";fitSingleLineMenuLabel(&text,102,12,.25f);assert(text.scale>small);assert(64*text.scale<=12+.001f);
 fitSingleLineMenuLabel(nullptr,50,12,.25f);
}
`));

test('survivor count selects full up to 16, reduced through 30 and compact above 30 with bounded geometry',()=>run(`
#include "domain/SurvivalBoardPresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){for(int alive:{0,1,5,12,16,17,26,30,31,40,64})for(int total:{16,26,64})for(float w:{320.f,568.f,1024.f})for(float h:{240.f,320.f,768.f}){
 auto g=survivalGridLayout(w,h,total,alive);assert(g.compact==(alive>30));assert(g.reduced==(alive>16&&alive<=30));assert(g.pages*g.pageSize>=static_cast<unsigned>(total));
 assert(g.columns*g.cellWidth<=w-36+.01f);assert(g.rows*g.cellHeight<=h-(g.compact?133:163)+.01f);
 auto card=survivalCardLayout(g.cellWidth,g.cellHeight,g.compact,g.reduced);assert(card.titleWidth>0&&card.titleHeight>0);assert((card.recordHeight==0)==g.compact);
 if(g.reduced){assert(card.recordY+card.recordHeight/2<=card.titleY-card.titleHeight/2+.001f);assert(card.avatarX+card.avatarSize/2<card.titleX-card.titleWidth/2);}
}assert(!survivalCompactCards(30)&&survivalCompactCards(31));assert(survivalReducedCards(17)&&survivalReducedCards(30)&&!survivalReducedCards(16)&&!survivalReducedCards(31));
 auto thirty=survivalGridLayout(568,320,30,30);assert(thirty.reduced&&thirty.pages==1&&thirty.pageSize==30);
 auto tied=survivalGridLayout(568,320,40,12);assert(!tied.compact&&tied.pages==3);
}
`));

test('actual single achievement title action toggles the equipped ID and keeps progressive selectors',()=>{
 const action=method(read('src/RankedPopup.cpp'),'    void onAchievementTitle(');
 run(`#include <cassert>
#include <optional>
#include <string>
#include <vector>
struct CCObject{int tag;int getTag()const{return tag;}};
struct Card{std::string id,type;};
struct RankedRuntime{struct{struct{std::vector<Card> cards;struct{bool active=false;std::string achievementId;}equippedTitle;}achievements;}v;
 int clears=0,equips=0;std::string last;static RankedRuntime&get(){static RankedRuntime r;return r;}auto const&view()const{return v;}
 void clearAchievementTitle(){++clears;v.achievements.equippedTitle.active=false;}
 void equipAchievementTitle(std::string id,std::optional<int>){++equips;last=id;}
};
struct Layer{std::string m_titleSelectorAchievement;${action}};
int main(){auto&r=RankedRuntime::get();r.v.achievements.cards={{"survival.hidden","ONE_SHOT"},{"solo.hidden","ONE_SHOT"},{"wins","PROGRESSIVE"}};Layer layer;CCObject a{0},b{1},c{2},invalid{99};
 r.v.achievements.equippedTitle={true,"survival.hidden"};layer.onAchievementTitle(&a);assert(r.clears==1&&r.equips==0);
 r.v.achievements.equippedTitle={true,"solo.hidden"};layer.onAchievementTitle(&a);assert(r.clears==1&&r.equips==1&&r.last=="survival.hidden");
 layer.onAchievementTitle(&b);assert(r.clears==2);layer.onAchievementTitle(&c);assert(layer.m_titleSelectorAchievement=="wins"&&r.equips==1);
 layer.onAchievementTitle(nullptr);layer.onAchievementTitle(&invalid);assert(r.clears==2&&r.equips==1);
}`);
});

test('actual progress fill animates earned IV/Legendary and preserves V rainbow while controls and lower tiers remain static',()=>run(`
#include <cassert>
#include <initializer_list>
struct Color{int r,g,b;};constexpr int EffectTag=1,kCCShader_PositionColor=2;
struct CCLayerColor{int stops=0,attached=0,tier=0;void stopActionByTag(int){++stops;attached=0;}void setShaderProgram(int){}void setColor(Color){}};
struct CCShaderCache{static CCShaderCache* sharedShaderCache(){static CCShaderCache c;return &c;}int programForKey(int){return 1;}};
Color achievementColor(int){return {1,2,3};}void attach(CCLayerColor*fill,int tier,bool solid){assert(solid);++fill->attached;fill->tier=tier;}
${method(read('src/ui/AchievementEffects.cpp'),'void styleAchievementFill(')}
int main(){for(int tier=0;tier<=5;++tier)for(bool progress:{false,true})for(bool earned:{false,true}){CCLayerColor bar;
 styleAchievementFill(&bar,tier,progress,earned);assert(bar.stops==1);assert(bar.attached==(progress&&(tier==5||(tier==4&&earned))));
 styleAchievementFill(&bar,2,true,true);assert(bar.stops==2&&bar.attached==0);
}styleAchievementFill(nullptr,4,true,true);}
`));

test('actual history level panel omits thumbnail nodes and context when unavailable and uses native metadata when present',()=>{
 const popup=read('src/RankedPopup.cpp');
 run(`#include "domain/MapMetadata.hpp"
#include "domain/MapDifficulty.hpp"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>
struct CCPoint{float x=0,y=0;};struct CCSize{float width=0,height=0;};struct Color{int r,g,b;};
std::string operator""_spr(char const*s,std::size_t n){return std::string(s,n);}
struct CCNode{CCSize size;CCPoint pos;std::string id;std::vector<CCNode*> children;void addChild(CCNode*n,int){children.push_back(n);}void setID(std::string s){id=s;}void setPosition(CCPoint p){pos=p;}};
struct CCLabelBMFont:CCNode{};struct CCSprite:CCNode{static CCSprite*create(){return new CCSprite;}};struct CCMenuItemSpriteExtra:CCNode{};
namespace fmt{std::string format(char const* pattern,int n){std::string s(pattern);auto p=s.find("{}");s.replace(p,2,std::to_string(n));return s;}}
constexpr Color kBlue{1,2,3},kDeepPanel2{1,2,3},kDeepPanel{1,2,3},kCyan{1,2,3},kGold{1,2,3};
namespace corum::ranked {
struct HistoryRound{int levelId=123,stars=7;std::string title="Exact Level",creator="Creator",key="r1",difficulty="HARDER",length="LONG",grade="LEGENDARY",label="R1";std::optional<int> featureScore=1,epic=2;};
struct SurvivalThumbnailProvider{static inline bool available=false;static bool supported(){return available;}};
namespace ui{constexpr Color Text{1,2,3},TextMuted{1,2,3};inline MapMetadata seen;void grayButtonVisual(CCNode*){}
CCNode*mapDifficultyBadge(int,MapMetadata metadata,float width,float height,bool native,std::optional<MapDifficulty> recorded){seen=metadata;assert(!native&&recorded&&recorded->nativeCode==4);auto*p=new CCNode;p->size={width,height};return p;}}
${method(read('src/domain/HistoryPresentation.hpp'),'inline float historyMapPanelHeight(')}
}
#define menu_selector(...) 0
struct CorumRankedLayer{CCSprite*m_historyThumbnail=nullptr;CCNode*m_historyThumbnailPlaceholder=nullptr;CCSize m_historyThumbnailSize;void*m_historyTexture=nullptr;std::string m_historySelectedId="match";std::uint64_t m_historyThumbnailGeneration=0;
struct Context{std::string match,round;int unused;std::uint64_t id,generation;}m_historyThumbnailContext;
int buttons=0,lastLevel=0;std::vector<std::string> labels;
std::string tr(char const*,char const*fallback)const{return fallback;}
CCNode*makeNeonPanel(CCSize size,CCPoint position,Color,Color,int){auto*n=new CCNode;n->size=size;n->pos=position;return n;}
CCLabelBMFont*historyText(CCNode*parent,std::string s,float,CCPoint position,float width,Color=corum::ranked::ui::Text,bool=false,CCPoint={.5f,.5f}){assert(width>0&&position.y>=0&&position.y<=parent->size.height);labels.push_back(s);auto*p=new CCLabelBMFont;parent->addChild(p,5);return p;}
CCMenuItemSpriteExtra*historyButton(CCNode*,std::string,CCPoint p,float,int,int level){assert(p.y>0);++buttons;lastLevel=level;return new CCMenuItemSpriteExtra;}
${method(popup,'    bool historyHasThumbnail(')}
${method(popup,'    void historyMapPanel(')}
};
int main(){using namespace corum::ranked;for(bool enabled:{false,true})for(int level:{0,123})for(float width:{156.f,206.f,300.f,390.f}){
 SurvivalThumbnailProvider::available=enabled;CorumRankedLayer layer;HistoryRound round;round.levelId=level;CCNode content;
 bool thumb=enabled&&level>0;auto h=historyMapPanelHeight(width,thumb);layer.historyMapPanel(&content,round,{width/2,h/2},width,h);
 assert(layer.buttons==1&&layer.lastLevel==level);assert((layer.m_historyThumbnail!=nullptr)==thumb);assert(layer.m_historyThumbnailGeneration==(thumb?1:0));
 assert(ui::seen.stars==7&&ui::seen.epic==2&&ui::seen.kind=="NORMAL");
 if(thumb)assert(layer.m_historyThumbnailContext.id==123&&layer.m_historyThumbnailContext.match=="match");
 bool frame=false;for(auto* node:content.children[0]->children)if(node->id=="ranked-history-thumbnail")frame=true;assert(frame==thumb);
}}
`);
});

test('actual reconnect modal sits above history, opens once and safely handles reconnect, cancel and detached owners',()=>{
 const update=method(read('src/RankedPopup.cpp'),'    void updateHistoryReconnect(');
 run(`#include <cassert>
#include <functional>
#include <optional>
#include <string>
namespace corum::ranked{bool historyReconnectNeeded(bool page,bool error,bool session,bool conflict){return page&&error&&!session&&!conflict;}}
enum class RuntimeStage{Ready,Loading,Error};struct RuntimeView{RuntimeStage stage=RuntimeStage::Error;bool accountActiveElsewhere=false;};
struct RankedRuntime{std::string token;int begins=0;static auto&get(){static RankedRuntime r;return r;}std::string spectatorLiveSessionToken(){return token;}void begin(){++begins;}};
template<class T>struct WeakRef{T*p;explicit WeakRef(T*v):p(v){}struct Locked{T*p;explicit operator bool()const{return p;}T*operator->(){return p;}};Locked lock()const{return {p};}};
std::string operator""_spr(char const*s,std::size_t n){return {s,n};}
namespace corum::ranked::ui{struct RankedConfirmPopup{inline static int creates=0;inline static RankedConfirmPopup*last=nullptr;std::function<void(bool)>done;bool shown=false;int z=0,cancellations=0;
 static auto*create(std::string,std::string,std::function<void(bool)> callback,std::string){++creates;auto*p=new RankedConfirmPopup;p->done=callback;return last=p;}
 void setID(std::string){}void show(){shown=true;}void setZOrder(int value){z=value;}void cancel(){++cancellations;shown=false;}};}
struct Layer{enum class Page{Live,HistoryList,HistoryDetail};Page m_page=Page::HistoryDetail;bool running=true,m_historyReconnectShown=false;
 std::optional<WeakRef<corum::ranked::ui::RankedConfirmPopup>>m_historyReconnect;std::string m_renderedUiKey="history";
 void closeHistoryFilter(){}
 bool isRunning(){return running;}std::string tr(char const*,char const*fallback){return fallback;}
 ${update}
};
int main(){using Popup=corum::ranked::ui::RankedConfirmPopup;RuntimeView view;Layer layer;auto&r=RankedRuntime::get();
 for(int i=0;i<100;++i){layer.updateHistoryReconnect(view);}assert(Popup::creates==1&&Popup::last->shown&&Popup::last->z>1000);
 Popup::last->done(true);assert(layer.m_page==Layer::Page::Live&&r.begins==1&&layer.m_renderedUiKey.empty()&&!layer.m_historyReconnect);
 Layer cancelled;cancelled.updateHistoryReconnect(view);Popup::last->done(false);assert(cancelled.m_page==Layer::Page::Live&&r.begins==1);
 Layer detached;detached.updateHistoryReconnect(view);detached.running=false;Popup::last->done(true);assert(r.begins==1&&detached.m_page==Layer::Page::HistoryDetail);
 auto count=Popup::creates;Layer valid;r.token="valid";valid.updateHistoryReconnect(view);assert(Popup::creates==count);r.token.clear();view.stage=RuntimeStage::Loading;valid.updateHistoryReconnect(view);assert(Popup::creates==count);
 view.stage=RuntimeStage::Error;view.accountActiveElsewhere=true;valid.updateHistoryReconnect(view);assert(Popup::creates==count);
 view.accountActiveElsewhere=false;Layer recovered;recovered.updateHistoryReconnect(view);auto*stale=Popup::last;r.token="recovered";recovered.updateHistoryReconnect(view);assert(stale->cancellations==1&&!recovered.m_historyReconnect);
 r.token.clear();recovered.updateHistoryReconnect(view);stale=Popup::last;view.accountActiveElsewhere=true;recovered.updateHistoryReconnect(view);assert(stale->cancellations==1&&!recovered.m_historyReconnect);
}
`);
});
