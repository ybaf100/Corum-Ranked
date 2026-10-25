import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const strip=s=>s.replace(/^#include .*?$|^#pragma once$/gm,'');
function run(cpp) {
    const directory=mkdtempSync(join(tmpdir(),'corum-leaderboard-'));
    try {
        writeFileSync(join(directory,'test.cpp'),cpp);
        execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(directory,'test.cpp'),'-o',join(directory,'test')],{stdio:'pipe'});
        execFileSync(join(directory,'test'),[],{stdio:'pipe'});
    } finally {rmSync(directory,{recursive:true,force:true});}
}

test('leaderboard columns reserve aligned status, badge, tier and LP; perimeter light travels clockwise at constant speed',()=>{
    run(`#include <cassert>
#include "domain/LeaderboardPresentation.hpp"
int main(){using namespace corum::ranked;
 for(float width:{448.0f,512.0f,528.0f}){
  auto c=leaderboardColumns(width);
  assert(c.rank+20<c.icon-12&&c.icon+12<c.name);
  assert(c.name+c.nameWidth<c.status&&c.status+c.statusWidth<c.tierIcon-13);
  assert(c.tierIcon+13<c.tierName&&c.tierName+c.tierWidth<c.value&&c.value+c.valueWidth<width);
 }
 LeaderboardEntry entry;assert(leaderboardValue(entry)=="— LP");entry.lp=150;assert(leaderboardValue(entry)=="150 LP");
 entry.promotion=true;entry.promotionGames=3;entry.marks={"WIN","LOSS"};assert(leaderboardValue(entry)=="O X _");
 entry.promotionGames=999;assert(leaderboardValue(entry)=="O X _ _ _ _ _ _");
 auto p=clockwiseBorderPoint(100,20,0);assert(p.x==0&&p.y==20);
 p=clockwiseBorderPoint(100,20,100.0/240);assert(p.x==100&&p.y==20);
 p=clockwiseBorderPoint(100,20,.5);assert(p.x==100&&p.y==0);
 p=clockwiseBorderPoint(100,20,220.0/240);assert(p.x==0&&p.y==0);
 p=clockwiseBorderPoint(100,20,1);assert(p.x==0&&p.y==20);
 p=clockwiseBorderPoint(100,20,-.01);assert(p.x==0&&p.y>17&&p.y<18);
 auto a=clockwiseBorderPoint(100,20,.05),b=clockwiseBorderPoint(100,20,.06);assert(std::abs((b.x-a.x)-2.4)<.001);
 p=clockwiseBorderPoint(0,0,.5);assert(p.x==0&&p.y==0);
 for(auto phase:{0.0,99.0/240,100.0/240,119.0/240,219.0/240,239.0/240,-.01,1.0}){
  auto stroke=clockwiseBorderStroke(100,20,phase,12);double length=0;
  assert(stroke.count>=1&&stroke.count<=3);
  for(std::size_t i=0;i<stroke.count;++i){auto s=stroke.segments[i];
   assert(s.from.x>=0&&s.from.x<=100&&s.to.x>=0&&s.to.x<=100);
   assert(s.from.y>=0&&s.from.y<=20&&s.to.y>=0&&s.to.y<=20);
   assert(s.from.x==s.to.x||s.from.y==s.to.y);
   assert(s.from.x!=s.to.x||s.from.y!=s.to.y);
   length+=std::hypot(s.to.x-s.from.x,s.to.y-s.from.y);
   if(i){auto last=stroke.segments[i-1].to;assert(std::abs(last.x-s.from.x)<.001&&std::abs(last.y-s.from.y)<.001);}
  }assert(std::abs(length-12)<.001);
  auto start=stroke.segments[0].from,end=clockwiseBorderPoint(100,20,phase+.5);
  assert(std::abs(start.x+end.x-100)<.001&&std::abs(start.y+end.y-20)<.001);
 }
 auto corner=clockwiseBorderStroke(100,20,99.0/240,12);assert(corner.count==2);
 assert(corner.segments[0].to.x==100&&corner.segments[0].to.y==20);
 assert(corner.segments[1].to.x==100&&corner.segments[1].to.y==9);
 auto lap=clockwiseBorderStroke(100,20,.1,999);assert(lap.count==5);
 double lapLength=0;for(std::size_t i=0;i<lap.count;++i)lapLength+=std::hypot(lap.segments[i].to.x-lap.segments[i].from.x,lap.segments[i].to.y-lap.segments[i].from.y);
 assert(std::abs(lapLength-240)<.001);
 for(float size:{0.0f,-1.0f})assert(clockwiseBorderStroke(size,20,.2,12).count==0);
 assert(clockwiseBorderStroke(100,20,0,0).count==0);
}`);
});

test('whole leaderboard popup validates pages, aligns statuses and icons, lazily resolves cosmetics and ignores responses after closing',()=>{
    // These are renderer/network boundaries. The Corum header and complete
    // translation unit compile and execute unchanged; this is no device claim.
    let boundary=read('tests/helpers/ResultPresentationNativeDouble.hpp').replace('#pragma once','')
        .replace('virtual void onEnter()', 'virtual bool init(){return true;} virtual void onEnter()')
        .replace('struct CCObject { virtual ~CCObject() = default; };', 'struct CCObject { virtual ~CCObject() = default; int tag=0; void setTag(int t){tag=t;} int getTag()const{return tag;} };')
        .replace('CCNode* getParent()',`CCNode*getChildByIDRecursive(std::string const&s){if(id==s)return this;for(auto*n:children)if(auto* found=n->getChildByIDRecursive(s))return found;return nullptr;}
        CCNode*getChildByID(std::string const&s){for(auto*n:children)if(n->id==s)return n;return nullptr;}
        CCPoint convertToWorldSpace(CCPoint p){p=p+position;return parent?parent->convertToWorldSpace(p):p;}
        CCPoint convertToNodeSpace(CCPoint p){auto o=convertToWorldSpace({});return {p.x-o.x,p.y-o.y};}
        CCNode* getParent()`)
        .replace('struct CCMenu : CCNode {',`struct CCRect{float x,y,width,height;};struct CCTouch{CCPoint p;int getID(){return 1;}CCPoint getLocation(){return p;}};struct CCEvent{};
        struct CCMenu : CCNode{virtual bool ccTouchBegan(CCTouch*,CCEvent*){return true;}virtual void ccTouchMoved(CCTouch*,CCEvent*){}virtual void ccTouchEnded(CCTouch*,CCEvent*){}virtual void ccTouchCancelled(CCTouch*,CCEvent*){}`)
        .replace('CCSize getWinSize() const', 'CCNode* getRunningScene(){static CCNode s;return &s;} CCNode* getNextScene(){return nullptr;} CCSize getWinSize() const')
        .replace('int segments=0;',`int segments=0;struct Line{CCPoint from,to;float radius;};std::vector<Line> lines;`)
        .replace('segments=0; polygon.clear();','segments=0; polygon.clear(); lines.clear();')
        .replace('from=a;to=b;radius=r;segmentColor=c;++segments;return true;',`assert(a.x!=b.x||a.y!=b.y);assert(std::abs(a.x-b.x)<.001||std::abs(a.y-b.y)<.001);
        lines.push_back({a,b,r});from=a;to=b;radius=r;segmentColor=c;++segments;return true;`)
        .replace('bool enabled=true; void setEnabled','void setNormalImage(CCNode*face){removeAllChildrenWithCleanup(true);addChild(face);} bool enabled=true; bool isEnabled()const{return enabled;} void setEnabled')
        .replace('static CCLayerColor* create(ccColor4B c)',`static CCLayerColor* create(ccColor4B c,float w,float h){auto* n=create(c);n->size={w,h};return n;}
        static CCLayerColor* create(ccColor4B c)`);
    const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
    const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'))
        .replace('bool isErr()const','bool isOk()const{return value.has_value();} bool isErr()const')
        .replace('bool isObject()const','bool isArray()const{return std::holds_alternative<Array>(data);} bool isObject()const');
    const header=strip(read('src/ui/RankedLeaderboardPopup.hpp')).replace('private:','public:');
    run(`
#include "domain/RankTierPresentation.hpp"
#include <chrono>
#include <cstdint>
#include <map>
#include <variant>
${boundary}
${json}
#include "domain/LeaderboardPresentation.hpp"
#include "domain/SurvivalHudPresentation.hpp"
#include "domain/RankedMode.hpp"
inline constexpr int GL_SRC_ALPHA=1,GL_ONE=1;
using SEL_MenuHandler=int;
namespace cocos2d{using ::CCSize;}
struct CCTouchDispatcher{int releases=0;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct GJAccountManager{int m_accountID=1;static GJAccountManager* sharedState(){static GJAccountManager m;return &m;}};
namespace geode{
enum class CommonFilter{Name};
struct TextInput:CCNode{std::string value;static TextInput*create(float width,char const*){auto*n=new TextInput;n->size={width,25};return n;}void setCommonFilter(CommonFilter){}void setMaxCharCount(int){}void setEnabled(bool){}std::string getString()const{return value;}void setString(std::string text){value=std::move(text);}};
struct Popup:CCNode{using CCNode::init;CCNode* m_bgSprite=new CCNode;CCMenuItemSpriteExtra* m_closeBtn=new CCMenuItemSpriteExtra;CCNode* m_mainLayer=new CCNode;
 bool init(float,float){addChild(m_mainLayer);return true;}virtual void onClose(CCObject*){removeFromParentAndCleanup(true);}void show(){static CCNode scene;scene.addChild(this);}};
struct ScrollLayer:CCNode{CCNode* m_contentLayer=new CCNode;
 ScrollLayer(CCRect r,bool,bool){size={r.width,r.height};addChild(m_contentLayer);}virtual bool ccTouchBegan(CCTouch*,CCEvent*){return true;}virtual void ccTouchMoved(CCTouch*,CCEvent*){}virtual void ccTouchEnded(CCTouch*,CCEvent*){}virtual void ccTouchCancelled(CCTouch*,CCEvent*){}
 static ScrollLayer* create(CCSize s){return new ScrollLayer({0,0,s.width,s.height},true,true);}void scrollToTop(){m_contentLayer->position={0,size.height-m_contentLayer->size.height};}};
namespace utils::web{
struct WebResponse{matjson::Value data;bool success=true;bool ok()const{return success;}matjson::Result<matjson::Value>json()const{return {data};}};
struct Operation{std::string url;};struct WebRequest{void timeout(std::chrono::seconds){}Operation get(std::string url){return {url};}};
struct Pending{Operation request;std::function<void(WebResponse)> callback;};inline std::vector<Pending> pending;
}
namespace async{template<class T>struct TaskHolder{int cancels=0;void cancel(){++cancels;}void spawn(utils::web::Operation req,std::function<void(T)> cb){utils::web::pending.push_back({req,std::move(cb)});}};}
namespace prelude{using geode::cast::typeinfo_cast;using geode::Popup;using geode::ScrollLayer;using geode::TextInput;using geode::CommonFilter;namespace web=geode::utils::web;}
}
namespace corum::ranked{
struct RankedRuntime{static RankedRuntime& get(){static RankedRuntime r;return r;}std::string leaderboardUrl(int o,int n,RankedMode mode)const{return fmt::format("{}?offset={}&limit={}",mode==RankedMode::Survival?"/api/ranked/survival/leaderboard":"/api/ranked/leaderboard",o,n);}};
namespace i18n{bool isKorean(){return false;}std::string text(char const*,char const* fallback){return fallback;}}
namespace ui{
void cancelMenuTouch(CCMenu* menu){if(menu)++menu->cancellations;}
struct SurvivalPlayerInfoPopup{static inline int opened=0,account=0;static inline bool survival=false;static SurvivalPlayerInfoPopup*createForLeaderboard(SurvivalStanding const& p,CCNode*,bool mode){account=p.accountId;survival=mode;static SurvivalPlayerInfoPopup instance;return &instance;}void show(){++opened;}};
enum class FontWeight{SemiBold,ExtraBold,Black};enum class RankBadgeDisplay{Matched,Leaderboard};
constexpr ccColor3B Text{242,247,255},TextMuted{164,181,205},Gold{255,207,74},Blue{77,135,255},Red{255,91,111},Green{90,238,151},Cyan{64,224,255},Surface{},SurfaceSoft{},SurfaceRaised{};
CCLabelBMFont* menuLabel(std::string const& t,float s,CCPoint p,ccColor3B c=Text,FontWeight=FontWeight::ExtraBold){auto* n=new CCLabelBMFont;n->setString(t.c_str());n->setScale(s);n->setPosition(p);n->setColor(c);return n;}
void fitMenuLabelWidth(CCLabelBMFont* n,float w,float s,float){n->setScale(std::min(s,w/n->size.width));}
CCLabelBMFont*publicPlayerTitle(CCNode*,matjson::Value const&,CCPoint,float,float){return nullptr;}
CCNode* surface(CCSize s,CCPoint p,ccColor3B accent,ccColor3B fill,int,bool){auto* n=CCNode::create();n->size=s;n->position=p;n->color=fill;auto* frame=CCNode::create();frame->color=accent;frame->id="frame";n->addChild(frame);return n;}
ccColor3B tierColor(std::string const& t){return t=="GOLD"?Gold:Blue;}
CCNode* standingNumber(std::optional<int> rank,float s){return menuLabel(rank?fmt::format("#{}",*rank):"#—",s,{});}
CCSprite* rankResourceSprite(char const*){return CCSprite::createWithSpriteFrameName("existing-glow");}
void fitRankSprite(CCSprite* n,CCSize s){n->size=s;}
inline int badges=0,iconCreates=0,iconExits=0;inline std::function<void()> duringBadge;
CCNode* createAnimatedRankBadge(std::string const&,CCSize s,RankBadgeDisplay display){assert(display==RankBadgeDisplay::Leaderboard);++badges;
 if(auto hook=std::exchange(duringBadge,{})){hook();}auto* n=CCNode::create();n->size=s;return n;}
class PlayerIcon:public CCNode{public:int account=0;bool local=false;static PlayerIcon* createLocal(){++iconCreates;auto* n=new PlayerIcon;n->local=true;return n;}static PlayerIcon* createRemote(int account){++iconCreates;auto* n=new PlayerIcon;n->account=account;return n;}void onExit()override{++iconExits;CCNode::onExit();}};
}}
${header}
${strip(read('src/ui/RankedLeaderboardPopup.cpp'))}
matjson::Value page(){using V=matjson::Value;V data;V::Array rows;
 for(int i=0;i<20;++i){V r;r["gdAccountId"]=std::to_string(i+1);r["isBot"]=i==3;r["player"]=std::string("Player-")+std::to_string(i);r["tier"]=i==3?"GOLD":"SILVER";r["rank"]=i+1;r["visibleRankedScore"]=1300-i;r["demotionWarning"]=i==1;
  if(i==2){r["promotion"]["gamesRequired"]=3;r["promotion"]["results"]=V::Array{"WIN","LOSS"};}
  rows.push_back(r);
 }data["players"]=rows;data["total"]=24;data["hasMore"]=true;return data;
}
int main(){using namespace corum::ranked;using namespace ui;namespace web=geode::utils::web;
 auto* p=RankedLeaderboardPopup::create();p->show();assert(p->m_busy&&web::pending.size()==1&&!p->m_next->isEnabled());
 assert(!p->m_refresh->isEnabled()&&p->m_refresh->opacity==100);
 for(int i=0;i<20;++i){p->onRefresh(nullptr);}assert(web::pending.size()==1);
 assert(web::pending[0].request.url=="/api/ranked/leaderboard?offset=0&limit=20");
 duringBadge=[&]{auto count=web::pending.size();for(int i=0;i<20;++i){p->onRefresh(nullptr);p->onNext(nullptr);}assert(web::pending.size()==count);};
 web::pending[0].callback({page()});assert(p->m_busy&&p->m_rows.empty()&&!p->m_refresh->isEnabled());
 p->onRefresh(nullptr);assert(web::pending.size()==1);drainMainQueue();
 assert(p->m_rows.size()==20&&badges==20&&!p->m_busy&&p->m_next->isEnabled()&&p->m_refresh->isEnabled());
 CCObject foreign;p->onPlayerInfo(&foreign);assert(SurvivalPlayerInfoPopup::opened==0);
 p->onPlayerInfo(p->m_rows[1].node);assert(SurvivalPlayerInfoPopup::opened==1&&SurvivalPlayerInfoPopup::account==2&&!SurvivalPlayerInfoPopup::survival);
 p->m_busy=true;p->onPlayerInfo(p->m_rows[2].node);assert(SurvivalPlayerInfoPopup::opened==1);p->m_busy=false;
 // Execute the production native scroll recognizer and deferred profile route,
 // including a response/page generation changing before finger-up is dispatched.
 auto* gestures=static_cast<LeaderboardScrollLayer*>(p->m_scroll);
 CCTouch finger{p->m_rows[1].node->convertToWorldSpace({100,24})};
 assert(gestures->ccTouchBegan(&finger,nullptr));gestures->ccTouchEnded(&finger,nullptr);
 assert(SurvivalPlayerInfoPopup::opened==1);drainMainQueue();assert(SurvivalPlayerInfoPopup::opened==2&&SurvivalPlayerInfoPopup::account==2);
 gestures->ccTouchEnded(&finger,nullptr);drainMainQueue();assert(SurvivalPlayerInfoPopup::opened==2);
 assert(gestures->ccTouchBegan(&finger,nullptr));gestures->ccTouchEnded(&finger,nullptr);++p->m_generation;drainMainQueue();assert(SurvivalPlayerInfoPopup::opened==2);
 assert(gestures->ccTouchBegan(&finger,nullptr));p->m_busy=true;p->updateButtons();gestures->ccTouchEnded(&finger,nullptr);p->m_busy=false;drainMainQueue();assert(SurvivalPlayerInfoPopup::opened==2);
 assert(iconCreates>0&&iconCreates<=5&&p->m_rows.front().icon->local&&p->m_rows[3].entry.bot);
 auto initialPosition=p->m_scroll->m_contentLayer->position;p->m_scroll->m_contentLayer->position.y+=80;p->update(0);
 assert(p->m_rows[3].icon&&p->m_rows[3].icon->account==0);p->m_scroll->m_contentLayer->position=initialPosition;p->update(0);
 auto c=leaderboardColumns(p->m_rows.front().node->size.width);
 for(auto const& row:p->m_rows){for(auto* child:row.node->children){auto* label=dynamic_cast<CCLabelBMFont*>(child);if(!label)continue;
  if(label->text.starts_with("Player-"))assert(label->position.x==c.name&&label->anchor.x==0);
  if(label->text=="PROMOTION MATCH"||label->text=="DEMOTION WARNING")assert(label->position.x==c.status);
  if(label->text=="SILVER"||label->text=="GOLD")assert(label->position.x==c.tierName);
 }}
 auto* shine=p->m_rows[2].shine;assert(shine&&p->m_rows[2].shine&&!p->m_rows[0].shine);
 assert(shine->lines.size()==6);auto top=shine->lines[2],bottom=shine->lines[5];
 for(auto const& line:shine->lines)assert(line.radius<=2.5f);
 auto w=p->m_rows[3].node->size.width;
 assert(std::abs(top.from.x+bottom.from.x-w)<.001&&std::abs(top.from.y+bottom.from.y-48)<.001);
 assert(top.from.y==top.to.y&&bottom.from.y==bottom.to.y);
 auto start=top.from.x;p->update(.1f);assert(shine->lines[2].from.x>start);
 // Both highlights bend at opposite corners without cutting through the card.
 p->m_elapsed=(w-.75-8)/(2*(w+48-1.5))*6;p->update(0);assert(shine->lines.size()==12);
 for(auto const& line:shine->lines){assert(line.from.x==line.to.x||line.from.y==line.to.y);}
 // Repeated refreshes before HTTP completion and while replacement is queued
 // leave a single request and the existing rows intact.
 auto* old=p->m_rows.front().node;p->onRefresh(nullptr);auto refresh=web::pending.back().callback;
 assert(web::pending.size()==2&&!p->m_refresh->isEnabled());
 for(int i=0;i<20;++i){p->onRefresh(nullptr);}assert(web::pending.size()==2&&p->m_rows.front().node==old);
 refresh({page()});p->onRefresh(nullptr);assert(web::pending.size()==2&&p->m_rows.front().node==old);
 drainMainQueue();assert(p->m_rows.front().node!=old&&!p->m_busy&&p->m_refresh->isEnabled());
 auto* replaced=p->m_rows.front().node;refresh({page()});drainMainQueue();assert(p->m_rows.front().node==replaced);
 p->m_scroll->m_contentLayer->position={};p->update(0);assert(!p->m_rows.front().icon&&iconExits>0);
 p->onNext(nullptr);assert(p->m_busy&&web::pending.size()==3&&web::pending[2].request.url.ends_with("offset=20&limit=20"));
 auto oversized=page();oversized["players"]=matjson::Value::Array(21,matjson::Value{});web::pending[2].callback({oversized});drainMainQueue();assert(!p->m_busy&&p->m_offset==0&&p->m_caption->color==Red&&p->m_refresh->isEnabled());
 p->onNext(nullptr);auto stale=web::pending.back().callback;stale({page()});p->dismiss();assert(p->m_closed&&!p->getParent()&&!p->scheduled&&CCTouchDispatcher::get()->releases==1);
 drainMainQueue();stale({page()});drainMainQueue();assert(p->m_offset==0);p->dismiss();p->onExit();assert(CCTouchDispatcher::get()->releases==1);
 auto* reopened=RankedLeaderboardPopup::create();reopened->show();auto next=web::pending.back().callback;matjson::Value empty;empty["players"]=matjson::Value::Array{};empty["total"]=0;empty["hasMore"]=false;
 next({empty});drainMainQueue();assert(reopened->m_rows.empty()&&!reopened->m_next->isEnabled()&&reopened->m_caption->text=="No players yet.");
 reopened->onRefresh(nullptr);auto oldSolo=web::pending.back().callback;
 reopened->onMode(reopened->m_survivalTab);assert(reopened->m_mode==corum::ranked::RankedMode::Survival&&reopened->m_offset==0&&reopened->m_busy&&reopened->m_rows.empty());
 assert(reopened->m_survivalTab->getNormalImage()->color==SurfaceRaised&&reopened->m_survivalTab->getNormalImage()->getChildByID("frame")->color==Gold);
 assert(reopened->m_soloTab->getNormalImage()->color==SurfaceRaised&&reopened->m_soloTab->getNormalImage()->getChildByID("frame")->color==Cyan);
 assert(web::pending.back().request.url.starts_with("/api/ranked/survival/leaderboard"));auto oldSur=web::pending.back().callback;
 reopened->onMode(reopened->m_soloTab);auto newest=web::pending.back().callback;
 assert(reopened->m_soloTab->getNormalImage()->color==SurfaceRaised&&reopened->m_soloTab->getNormalImage()->getChildByID("frame")->color==Gold);
 assert(reopened->m_survivalTab->getNormalImage()->color==SurfaceRaised&&reopened->m_survivalTab->getNormalImage()->getChildByID("frame")->color==Cyan);
 oldSolo({page()});oldSur({page()});drainMainQueue();assert(reopened->m_rows.empty()&&reopened->m_busy);
 newest({empty});drainMainQueue();assert(!reopened->m_busy&&reopened->m_mode==corum::ranked::RankedMode::SoloRanked);
 reopened->onRefresh(nullptr);auto failure=web::pending.back().callback;failure({{},false});drainMainQueue();assert(!reopened->m_busy&&reopened->m_refresh->isEnabled());reopened->dismiss();
}`);
    const popup=read('src/RankedPopup.cpp');
    assert.match(popup,/ranked-leaderboard-trophy/);
    assert.match(popup,/m_leaderboard->lock\(\)\) popup->dismiss\(\)/);
});

test('leaderboard dims only the SILVER circular halo and preserves matched-card effects',()=>{
    const badges=read('src/ui/RankBadgeAssets.cpp');
    const ambient=badges.slice(badges.indexOf('bool hasPersistentRankAmbient('),badges.indexOf('CCNode* createRankBadgeBackdrop('));
    const display=read('src/ui/RankBadgeAssets.hpp').match(/enum class RankBadgeDisplay\s*\{[^}]+\};/)[0];
    run(`
#include "domain/RankTierPresentation.hpp"
#include <array>
#include <map>
${read('tests/helpers/ResultPresentationNativeDouble.hpp').replace('#pragma once','')}
struct CCRepeatForever{static CCAction* create(CCAction* a){auto* n=action("repeat",a->duration);n->children={a};return n;}};
struct CCFadeTo{static CCAction* create(float d,GLubyte opacity){return action("fade",d,static_cast<float>(opacity));}};
namespace corum::ranked::ui{
${display}
std::map<CCSprite*,std::array<int,2>> pulses;
std::string normalizeRankName(std::string rank){return rank;}
CCNode* centeredRoot(){return CCNode::create();}
CCSprite* ambientSprite(CCNode* root,std::string const& file,CCSize size,int z,GLubyte opacity){
 auto* n=CCSprite::createWithSpriteFrameName(file.c_str());n->setContentSize(size);n->setOpacity(opacity);root->addChild(n,z);return n;}
void pulseOpacity(CCSprite* n,GLubyte low,GLubyte high,float){if(n)pulses[n]={low,high};}
${ambient}
}
CCSprite* named(CCNode* n,std::string const& name){for(auto* child:n->children)if(child->id==name)return static_cast<CCSprite*>(child);assert(false);return nullptr;}
int main(){using namespace corum::ranked::ui;
 auto* matched=createRankAmbientFx("SILVER",{27,27},RankBadgeDisplay::Matched);
 auto* leaderboard=createRankAmbientFx("SILVER",{27,27},RankBadgeDisplay::Leaderboard);
 auto* mh=named(matched,"fx-silver-halo.png"),*lh=named(leaderboard,"fx-silver-halo.png");
 assert(lh->opacity<mh->opacity/3&&pulses[lh][1]<pulses[mh][1]/3);
 assert(lh->size.width==mh->size.width&&lh->size.height==mh->size.height);
 for(auto file:{"fx-silver-orbit.png","fx-silver-crystal-sheen.png"}){
  auto* m=named(matched,file),*l=named(leaderboard,file);
  assert(m->opacity==l->opacity&&m->size.width==l->size.width&&m->actions.size()==l->actions.size());
 }
 auto* gold=createRankAmbientFx("GOLD",{27,27},RankBadgeDisplay::Matched);
 auto* listGold=createRankAmbientFx("GOLD",{27,27},RankBadgeDisplay::Leaderboard);
 assert(gold->children.size()==listGold->children.size());
 for(auto* child:gold->children){auto* m=static_cast<CCSprite*>(child),*l=named(listGold,m->id);
  assert(m->opacity==l->opacity&&m->size.width==l->size.width&&pulses[m]==pulses[l]);
 }
}`);
});
