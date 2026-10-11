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
}`);
});

test('whole leaderboard popup validates pages, aligns statuses and icons, lazily resolves cosmetics and ignores responses after closing',()=>{
    // These are renderer/network boundaries. The Corum header and complete
    // translation unit compile and execute unchanged; this is no device claim.
    let boundary=read('tests/helpers/ResultPresentationNativeDouble.hpp').replace('#pragma once','')
        .replace('CCNode* getParent()',`CCPoint convertToWorldSpace(CCPoint p){p=p+position;return parent?parent->convertToWorldSpace(p):p;}
        CCPoint convertToNodeSpace(CCPoint p){auto o=convertToWorldSpace({});return {p.x-o.x,p.y-o.y};}
        CCNode* getParent()`)
        .replace('struct CCSprite : CCNode {','struct CCSprite : CCNode { void setBlendFunc(std::pair<int,int>) {}')
        .replace('bool enabled=true; void setEnabled','bool enabled=true; bool isEnabled()const{return enabled;} void setEnabled')
        .replace('static CCLayerColor* create(ccColor4B c)',`static CCLayerColor* create(ccColor4B c,float w,float h){auto* n=create(c);n->size={w,h};return n;}
        static CCLayerColor* create(ccColor4B c)`);
    const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');
    const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'))
        .replace('bool isErr()const','bool isOk()const{return value.has_value();} bool isErr()const')
        .replace('bool isObject()const','bool isArray()const{return std::holds_alternative<Array>(data);} bool isObject()const');
    const header=strip(read('src/ui/RankedLeaderboardPopup.hpp')).replace('private:','public:');
    run(`
#include <chrono>
#include <cstdint>
#include <map>
#include <variant>
${boundary}
${json}
#include "domain/LeaderboardPresentation.hpp"
inline constexpr int GL_SRC_ALPHA=1,GL_ONE=1;
using SEL_MenuHandler=int;
namespace cocos2d{using ::CCSize;}
struct CCTouchDispatcher{int releases=0;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct GJAccountManager{int m_accountID=1;static GJAccountManager* sharedState(){static GJAccountManager m;return &m;}};
namespace geode{
struct Popup:CCNode{CCNode* m_bgSprite=new CCNode;CCMenuItemSpriteExtra* m_closeBtn=new CCMenuItemSpriteExtra;CCNode* m_mainLayer=new CCNode;
 bool init(float,float){addChild(m_mainLayer);return true;}virtual void onClose(CCObject*){removeFromParentAndCleanup(true);}void show(){static CCNode scene;scene.addChild(this);}};
struct ScrollLayer:CCNode{CCNode* m_contentLayer=new CCNode;static ScrollLayer* create(CCSize s){auto* n=new ScrollLayer;n->size=s;n->addChild(n->m_contentLayer);return n;}void scrollToTop(){m_contentLayer->position={0,size.height-m_contentLayer->size.height};}};
namespace utils::web{
struct WebResponse{matjson::Value data;bool success=true;bool ok()const{return success;}matjson::Result<matjson::Value>json()const{return {data};}};
struct Operation{std::string url;};struct WebRequest{void timeout(std::chrono::seconds){}Operation get(std::string url){return {url};}};
struct Pending{Operation request;std::function<void(WebResponse)> callback;};inline std::vector<Pending> pending;
}
namespace async{template<class T>struct TaskHolder{int cancels=0;void cancel(){++cancels;}void spawn(utils::web::Operation req,std::function<void(T)> cb){utils::web::pending.push_back({req,std::move(cb)});}};}
namespace prelude{using geode::Popup;using geode::ScrollLayer;namespace web=geode::utils::web;}
}
namespace corum::ranked{
struct RankedRuntime{static RankedRuntime& get(){static RankedRuntime r;return r;}std::string leaderboardUrl(int o,int n)const{return fmt::format("/api/ranked/leaderboard?offset={}&limit={}",o,n);}};
namespace i18n{bool isKorean(){return false;}std::string text(char const*,char const* fallback){return fallback;}}
namespace ui{
enum class FontWeight{ExtraBold,Black};enum class RankBadgeDisplay{Matched};
constexpr ccColor3B Text{242,247,255},TextMuted{164,181,205},Gold{255,207,74},Blue{77,135,255},Red{255,91,111},Green{90,238,151},Cyan{64,224,255},Surface{},SurfaceSoft{},SurfaceRaised{};
CCLabelBMFont* menuLabel(std::string const& t,float s,CCPoint p,ccColor3B c=Text,FontWeight=FontWeight::ExtraBold){auto* n=new CCLabelBMFont;n->setString(t.c_str());n->setScale(s);n->setPosition(p);n->setColor(c);return n;}
void fitMenuLabelWidth(CCLabelBMFont* n,float w,float s,float){n->setScale(std::min(s,w/n->size.width));}
CCNode* surface(CCSize s,CCPoint p,ccColor3B,ccColor3B,int,bool){auto* n=CCNode::create();n->size=s;n->position=p;return n;}
ccColor3B tierColor(std::string const& t){return t=="GOLD"?Gold:Blue;}
CCNode* standingNumber(std::optional<int> rank,float s){return menuLabel(rank?fmt::format("#{}",*rank):"#—",s,{});}
CCSprite* rankResourceSprite(char const*){return CCSprite::createWithSpriteFrameName("existing-glow");}
void fitRankSprite(CCSprite* n,CCSize s){n->size=s;}
inline int badges=0,iconCreates=0,iconExits=0;
CCNode* createAnimatedRankBadge(std::string const&,CCSize s,RankBadgeDisplay display){assert(display==RankBadgeDisplay::Matched);++badges;auto* n=CCNode::create();n->size=s;return n;}
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
 assert(web::pending[0].request.url=="/api/ranked/leaderboard?offset=0&limit=20");
 web::pending[0].callback({page()});assert(p->m_rows.size()==20&&badges==20&&!p->m_busy&&p->m_next->isEnabled());
 assert(iconCreates>0&&iconCreates<=5&&p->m_rows.front().icon->local&&p->m_rows[3].icon->account==0);
 auto c=leaderboardColumns(p->m_rows.front().node->size.width);
 for(auto const& row:p->m_rows){for(auto* child:row.node->children){auto* label=dynamic_cast<CCLabelBMFont*>(child);if(!label)continue;
  if(label->text.starts_with("Player-"))assert(label->position.x==c.name&&label->anchor.x==0);
  if(label->text=="PROMOTION MATCH"||label->text=="DEMOTION WARNING")assert(label->position.x==c.status);
  if(label->text=="SILVER"||label->text=="GOLD")assert(label->position.x==c.tierName);
 }}
 auto* shine=p->m_rows[3].shine;assert(shine&&p->m_rows[2].shine&&!p->m_rows[0].shine);
 assert(shine->position.x==0&&shine->position.y==48);p->update(.1f);assert(shine->position.x>0&&shine->position.y==48);
 p->m_scroll->m_contentLayer->position={};p->update(0);assert(!p->m_rows.front().icon&&iconExits>0);
 p->onNext(nullptr);assert(p->m_busy&&web::pending.size()==2&&web::pending[1].request.url.ends_with("offset=20&limit=20"));
 auto oversized=page();oversized["players"]=matjson::Value::Array(21,matjson::Value{});web::pending[1].callback({oversized});assert(!p->m_busy&&p->m_offset==0&&p->m_caption->color==Red);
 p->onNext(nullptr);auto stale=web::pending.back().callback;p->dismiss();assert(p->m_closed&&!p->getParent()&&!p->scheduled&&CCTouchDispatcher::get()->releases==1);
 stale({page()});assert(p->m_offset==0);p->dismiss();p->onExit();assert(CCTouchDispatcher::get()->releases==1);
 auto* reopened=RankedLeaderboardPopup::create();reopened->show();auto next=web::pending.back().callback;matjson::Value empty;empty["players"]=matjson::Value::Array{};empty["total"]=0;empty["hasMore"]=false;
 next({empty});assert(reopened->m_rows.empty()&&!reopened->m_next->isEnabled()&&reopened->m_caption->text=="No players yet.");reopened->dismiss();
}`);
    const popup=read('src/RankedPopup.cpp');
    assert.match(popup,/ranked-leaderboard-trophy/);
    assert.match(popup,/m_leaderboard->lock\(\)\) popup->dismiss\(\)/);
});
