import {fileURLToPath} from 'node:url';
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const read=p=>readFileSync(new URL(p,import.meta.url),'utf8');
test('actual information popup waits for foreign GD delegates, checks account identity, and closes before native entry',()=>{
  // Native UI and GD network are boundary doubles. Compile the whole Corum
  // popup, rather than substituting its request/lifetime logic.
  const source=read('../src/ui/RankedPlayerInfoPopup.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'')
    .replace('class RankedPlayerInfoPopup final : public Popup, public UserInfoDelegate {',
      'class RankedPlayerInfoPopup final : public Popup, public UserInfoDelegate { public:');
  const cpp=`
#include <cassert>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <algorithm>
${read('../src/domain/PlayerInfoPresentation.hpp').replace('#pragma once','')}
struct CCObject{virtual ~CCObject()=default;};struct CCPoint{float x,y;};struct ccColor3B{unsigned char r,g,b;};
struct CCNode:CCObject{bool parent=true,visible=true;static CCNode* create(){return new CCNode;}void addChild(CCNode*,int=0){}void removeFromParentAndCleanup(bool){parent=false;}void* getParent(){return parent?this:nullptr;}void setVisible(bool v){visible=v;}void setID(std::string const&){}void setPosition(CCPoint){}void autorelease(){}void schedule(int,float){}void unschedule(int){}};
struct CCLabelBMFont:CCNode{std::string text;inline static std::vector<std::string> labels;void setAnchorPoint(CCPoint){}};
struct CCMenu:CCNode{static CCMenu* create(){return new CCMenu;}};
struct CCMenuItemSpriteExtra:CCNode{static CCMenuItemSpriteExtra* create(CCNode*,CCNode*,int){return new CCMenuItemSpriteExtra;}};
const CCPoint CCPointZero{0,0};
struct CCTouchDispatcher{int releases=0;static CCTouchDispatcher* get(){static CCTouchDispatcher d;return &d;}void unregisterForcePrio(CCNode*){++releases;}};
struct Popup:CCNode{CCNode* m_bgSprite=new CCNode;CCNode* m_closeBtn=new CCNode;CCNode* m_mainLayer=new CCNode;bool init(float,float){return true;}virtual void onClose(CCObject*){parent=false;onExit();}virtual void onExit(){}};
template<class T>struct Ref{explicit Ref(T*){}};
struct GJUserScore{int m_accountID=123,m_globalRank=0,m_stars=42,m_moons=12,m_demons=5;std::string m_demonInfo="0,0,0,0,3,0,0,0,0,2,0,0";};
struct UserInfoDelegate{virtual ~UserInfoDelegate()=default;virtual void getUserInfoFinished(GJUserScore*){}virtual void getUserInfoFailed(int){}virtual void userInfoChanged(GJUserScore*){}};
struct GameLevelManager{UserInfoDelegate* m_userInfoDelegate=nullptr;int requests=0,account=0;GJUserScore* cached=nullptr;static GameLevelManager* sharedState(){static GameLevelManager m;return &m;}GJUserScore* userInfoForAccountID(int id){return cached&&cached->m_accountID==id?cached:nullptr;}void getGJUserInfo(int id){++requests;account=id;}};
#define schedule_selector(x) 0
#define menu_selector(x) 0
std::string operator""_spr(char const* s,std::size_t n){return std::string(s,n);}
namespace geode::prelude{}
namespace fmt{template<class T>std::string format(char const* pattern,T value){std::string s(pattern);auto p=s.find("{}");if(p!=std::string::npos)s.replace(p,2,std::to_string(value));return s;}}
namespace corum::ranked{enum class RuntimeStage{Idle,Matched};struct ForumRankView{std::string provider,externalName,status="PENDING",lastSuccessAt;std::optional<int> rank;};struct ForumRecordsView{bool loaded=false,loading=false;std::string error;std::vector<ForumRankView> records;};struct PlayerInfoView{int accountId=123;std::string username="Human",side="A";bool botReference=false;ForumRecordsView forum;};
struct RankedRuntime{struct View{RuntimeStage stage=RuntimeStage::Matched;std::uint64_t revision=0;struct {std::string matchId="m",state="ROUND_PREPARE"; struct Profile{std::string displayedTier="RED";int wins=8,losses=2;} playerAProfile,playerBProfile;}match;PlayerInfoView playerInfo;}v;int infoReads=0;static RankedRuntime& get(){static RankedRuntime r;return r;}auto const& view()const{return v;}void fetchPlayerInfo(std::string const& side){++infoReads;v.playerInfo.side=side;}};
namespace i18n{bool isKorean(){return false;}std::string text(char const*,char const* fallback){return fallback;}}
namespace ui{enum class FontWeight{SemiBold,ExtraBold,Black};constexpr ccColor3B Cyan{1,2,3},Blue{1,2,3},Surface{1,2,3},SurfaceRaised{1,2,3},Text{1,2,3},TextMuted{1,2,3},Gold{1,2,3};
CCNode* surface(CCPoint,CCPoint,ccColor3B,ccColor3B,int,bool){return new CCNode;}
CCLabelBMFont* menuLabel(std::string const& text,float,CCPoint,ccColor3B,FontWeight=FontWeight::SemiBold){auto* n=new CCLabelBMFont;n->text=text;CCLabelBMFont::labels.push_back(text);return n;}
void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}
enum class RankBadgeDisplay{Compact};inline int haloBadges=0;
bool hasPersistentRankAmbient(std::string const& tier){return tier=="SILVER"||tier=="GOLD";}
CCNode* createAnimatedRankBadge(std::string const&,CCPoint,RankBadgeDisplay){++haloBadges;return new CCNode;}

}}
${source}
int main(){using namespace corum::ranked;using ui::RankedPlayerInfoPopup;auto* manager=GameLevelManager::sharedState();auto& runtime=RankedRuntime::get();UserInfoDelegate foreign;
 manager->m_userInfoDelegate=&foreign;auto* p=RankedPlayerInfoPopup::create("A");assert(p&&manager->requests==0&&manager->m_userInfoDelegate==&foreign);
 assert(std::find(CCLabelBMFont::labels.begin(),CCLabelBMFont::labels.end(),"8W / 2L (80%)")!=CCLabelBMFont::labels.end());
 manager->m_userInfoDelegate=nullptr;p->refresh(0);assert(manager->requests==1&&manager->account==123&&manager->m_userInfoDelegate==p);
 GJUserScore wrong;wrong.m_accountID=456;p->getUserInfoFinished(&wrong);assert(!p->m_statistics&&manager->m_userInfoDelegate==p);
 GJUserScore score;p->getUserInfoFinished(&score);assert(p->m_statistics&&p->m_statistics->extreme==5&&manager->m_userInfoDelegate==nullptr);
 auto before=CCLabelBMFont::labels.size();p->render();for(auto i=before;i<CCLabelBMFont::labels.size();++i)assert(CCLabelBMFont::labels[i]!="GD Global Rank");
 score.m_globalRank=10;p->getUserInfoFinished(&score);assert(CCLabelBMFont::labels.end()!=std::find(CCLabelBMFont::labels.begin(),CCLabelBMFont::labels.end(),"#10"));
 runtime.v.playerInfo.forum.loaded=true;runtime.v.playerInfo.forum.records.push_back({"GDKF","","PENDING","",{}});
 auto reads=runtime.infoReads;for(int i=0;i<10;++i)p->refresh(0);assert(runtime.infoReads==reads+1&&manager->requests==1);
 runtime.v.match.state="ROUND_ENTERING";p->refresh(0);assert(p->m_closed&&!p->parent&&CCTouchDispatcher::get()->releases==1);delete p;
 runtime.v.match.state="ROUND_PREPARE";runtime.v.match.playerBProfile.displayedTier="GOLD";auto* second=RankedPlayerInfoPopup::create("B");assert(second&&manager->m_userInfoDelegate==second&&ui::haloBadges>0);
 manager->m_userInfoDelegate=&foreign;second->onExit();assert(manager->m_userInfoDelegate==&foreign);delete second;
 runtime.v.match.state="DEATHMATCH_ENTERING";assert(!RankedPlayerInfoPopup::create("A"));
}
`;
  const dir=mkdtempSync(join(tmpdir(),'corum-player-info-popup-'));
  try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',fileURLToPath(new URL('../src/domain',import.meta.url)),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
  assert.match(read('../src/RankedPopup.cpp'), /m_playerInfo->lock\(\)/);
});
