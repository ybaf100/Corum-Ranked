import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {fileURLToPath} from 'node:url';
const root=fileURLToPath(new URL('..',import.meta.url));
const read=p=>readFileSync(join(root,p),'utf8');
function run(code){const dir=mkdtempSync(join(tmpdir(),'achievement-v2-'));try{writeFileSync(join(dir,'test.cpp'),code);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
const preamble=`#include "domain/AchievementStyle.hpp"
#include <cassert>
#include <optional>
#include <string>
#include <vector>
using namespace corum::ranked;
struct Reward{std::optional<int> level;int rewardAp;std::string unlockedAt="server",claimedAt="";};
struct Card{std::string scope="SURVIVAL",type="PROGRESSIVE",rarity;bool unlocked=false;int currentLevel=0;std::vector<Reward> unlocks;};
`;
test('Claim All uses actual unclaimed stages within the selected scope, independently of the highest attained stage',()=>run(preamble+`
int main(){Card c;c.currentLevel=5;c.unlocks={{4,40,"server",""},{5,75,"server","claimed"}};Card solo;solo.scope="SOLO_RANKED";solo.unlocks={{5,75}};
 auto p=achievementPending(std::vector{c,solo},"SURVIVAL");assert(p.tier==4&&p.amount==40&&p.rewards==1);
 c.unlocks[0].claimedAt="claimed";p=achievementPending(std::vector{c,solo},"SURVIVAL");assert(p.tier==0&&p.amount==0&&p.rewards==0);
 assert(achievementPending(std::vector{c,solo},"ALL").tier==5);
}`));
test('Mythic → Legendary → Epic → disabled follows the remaining Unlocks and never invents an unconfirmed reward',()=>run(preamble+`
int main(){Card c;c.type="ONE_SHOT";c.rarity="MYTHIC";c.unlocked=true;c.unlocks={{std::nullopt,200}};auto l=c;l.rarity="LEGENDARY";l.unlocks[0].rewardAp=100;auto e=l;e.rarity="EPIC";e.unlocks[0].rewardAp=50;
 std::vector cards{c,l,e};assert(achievementPending(cards,"SURVIVAL").tier==5);cards[0].unlocks[0].claimedAt="yes";assert(achievementPending(cards,"SURVIVAL").tier==4);cards[1].unlocks[0].claimedAt="yes";assert(achievementPending(cards,"SURVIVAL").tier==3);cards[2].unlocks[0].claimedAt="yes";assert(achievementPending(cards,"SURVIVAL").tier==0);
 Card malformed;malformed.currentLevel=5;malformed.unlocks={{6,75},{5,75,"",""}};assert(achievementPending(std::vector{malformed},"SURVIVAL").rewards==0);
}`));
test('locked one-shots keep rarity colors; unearned progressive stages remain dark gray',()=>run(preamble+`
int main(){Card c;c.type="ONE_SHOT";c.rarity="MYTHIC";assert(achievementCardTier(c)==5);Card progressive;assert(achievementCardTier(progressive)==0);auto locked=achievementTierColor(0);assert(locked.r==92&&locked.g==100&&locked.b==116);
 int rgb[][3]={{178,186,201},{87,187,255},{198,121,255},{255,176,70}};for(int i=1;i<=4;i++){auto a=achievementTierColor(i);assert(a.r==rgb[i-1][0]&&a.g==rgb[i-1][1]&&a.b==rgb[i-1][2]);}c.unlocked=true;assert(achievementCardTier(c)==5);
}`));
test('pastel flow is continuous inside text and over the three-second loop, using all seven exact colors',()=>run(preamble+`
int main(){for(std::size_t i=0;i<7;i++){auto c=achievementPastelAt(double(i)/7,0);auto expected=AchievementPastels[i];assert(std::abs(c.r-expected.r)<.001&&std::abs(c.g-expected.g)<.001&&std::abs(c.b-expected.b)<.001);}
 for(int frame=0;frame<1800;frame++){auto a=achievementPastelAt(.123,frame/60.);auto b=achievementPastelAt(.123,(frame+1)/60.);assert(std::abs(a.r-b.r)<4&&std::abs(a.g-b.g)<4&&std::abs(a.b-b.b)<4);auto c=achievementPastelAt(.123,frame/60.+3);assert(std::abs(c.r-a.r)<.001&&std::abs(c.g-a.g)<.001&&std::abs(c.b-a.b)<.001);}
 auto a=achievementPastelAt(.2,0),b=achievementPastelAt(.201,0);assert(std::abs(a.g-b.g)<1);
}`));
test('native effect owns a per-target shader/action, preserves glyph masks and stops the old action before retiering',()=>{
 const source=read('src/ui/AchievementEffects.cpp');assert.match(source,/texture2D\(CC_Texture0,v_uv\)/);assert.match(source,/v_x=a_position.x/);assert.match(source,/new CCGLProgram/);assert.match(source,/stopActionByTag\(EffectTag\)/);
 assert.match(source,/CCRepeatForever::create\(flow\)/);assert.doesNotMatch(source,/queueInMainThread|schedule\(|addChild\(/);assert.match(source,/shader unavailable/);
 const popup=read('src/RankedPopup.cpp');assert.match(popup,/achievementPending\(view.achievements.cards,"ALL"\)/);assert.match(popup,/claimAllAchievements\("ALL"\)/);
 const runtime=read('src/RankedRuntime.cpp');const action=runtime.slice(runtime.indexOf('void RankedRuntime::claimAllAchievements'),runtime.indexOf('void RankedRuntime::equipAchievementTitle'));
 assert.match(action,/generation!=m_sessionGeneration/);assert.match(action,/session!=m_sessionToken/);assert.match(action,/fetchAchievements\(\)/);assert.doesNotMatch(action,/achievementUnlocks|enqueueAchievementToasts/);
});

test('actual Claim All transport blocks duplicates, sends the chosen scope, retires stale session callbacks and refreshes server state once',()=>{
 const source=read('src/RankedRuntime.cpp');let a=source.indexOf('void RankedRuntime::claimAllAchievements('),b=source.indexOf('{',a)+1,depth=1;while(depth){depth+=(source[b]==='{')-(source[b]==='}');b++;}
 const viewer=read('tests/SpectatorLiveViewerNativeTest.mjs');const json=viewer.slice(viewer.indexOf('namespace matjson {'),viewer.indexOf('${readFileSync(join(root,'));
 run(`#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>
#include <cassert>
${json}
namespace web{struct WebResponse{int status;matjson::Value body;matjson::Result<matjson::Value>json(){return {body};}};struct Request{matjson::Value body;void bodyJSON(matjson::Value v){body=v;}std::pair<std::string,matjson::Value>post(std::string p){return {p,body};}};}
bool successful(web::WebResponse r){return r.status>=200&&r.status<300;}std::string responseError(web::WebResponse){return "retry";}
web::Request baseRequest(std::string const&){return {};}
struct Pending{std::pair<std::string,matjson::Value> request;std::function<void(web::WebResponse)> callback;};
struct RequestHolder{std::vector<Pending>pending;void spawn(std::pair<std::string,matjson::Value>r,std::function<void(web::WebResponse)>c){pending.push_back({r,c});}};
struct RuntimeReward{std::optional<int>level;std::string claimedAt;int rewardAp=0;};struct RuntimeCard{std::string id,type;std::vector<RuntimeReward>unlocks;int claimableAp=0,claimableRewards=0;bool claimed=false;};
struct RankedRuntime{bool m_controlBusy=false,m_rankedOpen=true;std::string m_sessionToken="session";std::uint64_t m_sessionGeneration=1,m_claimNoticeSequence=0;
 struct View{struct State{bool loading=false;std::string error;int totalAp=0,unclaimedRewardCount=3,claimedAmount=0,claimedRewards=0;std::uint64_t claimNoticeSequence=0;std::vector<RuntimeCard>cards;}achievements;int revision=0;}m_view;RequestHolder m_controlRequest;int fetched=0;
 void observeServerNow(matjson::Value const&){}std::string endpoint(std::string p){return p;}void fetchAchievements(){++fetched;m_view.achievements.loading=true;}
 void claimAllAchievements(std::string const&);
};
${source.slice(a,b)}
int main(){RankedRuntime r;r.claimAllAchievements("FORGED");assert(r.m_controlRequest.pending.empty());r.claimAllAchievements("ALL");r.claimAllAchievements("ALL");r.claimAllAchievements("SURVIVAL");assert(r.m_controlRequest.pending.size()==1);
 auto p=r.m_controlRequest.pending[0];assert(p.request.first=="/api/ranked/achievements/claim-all"&&p.request.second["scope"].asString().unwrapOr("")=="ALL");
 p.callback({503,{}});assert(!r.m_controlBusy&&!r.m_view.achievements.loading&&r.fetched==0&&r.m_view.achievements.claimNoticeSequence==0);
 r.claimAllAchievements("SURVIVAL");auto stale=r.m_controlRequest.pending[1].callback;++r.m_sessionGeneration;r.m_sessionToken="replacement";matjson::Value body;body["claimedAmount"]=200;body["claimedRewards"]=1;body["totalAp"]=300;body["unclaimedRewardCount"]=2;stale({201,body});assert(r.fetched==0&&r.m_view.achievements.totalAp==0);
 r.m_controlBusy=false;r.claimAllAchievements("SHARED");r.m_controlRequest.pending[2].callback({201,body});assert(r.fetched==1&&r.m_view.achievements.claimNoticeSequence==0&&r.m_view.achievements.totalAp==300);
}`);
});

test('achievement two-column cards, Claim All row and pagination stay separated on iPad/mobile/wide viewports',()=>run(`
#include "domain/AchievementScreenLayout.hpp"
#include <cassert>
int main(){for(float w:{400.f,432.f,568.f,640.f,900.f})for(float h:{280.f,300.f,320.f,480.f}){
 auto r=corum::ranked::achievementScreenLayout(w,h);auto const top=r.firstY+r.cardHeight/2,bottom=r.firstY-r.rowStep-r.cardHeight/2;
 assert(top<=h-114 && bottom>=39.9f && r.cardScale>0&&r.cardScale<=1);
 assert(w/4-r.cardWidth/2>=10 && 3*w/4+r.cardWidth/2<=w-10);
 auto apRight=w*.16f+w*.19f/2, pendingLeft=w*.40f-w*.23f/2,pendingRight=w*.40f+w*.23f/2,claimLeft=w*.755f-std::min(210.f,w*.38f)/2;
 assert(apRight<pendingLeft&&pendingRight<claimLeft);
}}
`));
test('all v2 Korean catalog names/descriptions are present in all four active bilingual fonts',()=>{
 const catalog=JSON.parse(read('../ranked/translations/achievements.json'));const names=Object.values(catalog).flatMap(v=>Object.values(v).map(pair=>pair.ko)).join('');
 const fonts=JSON.parse(read('mod.json')).resources.fonts;
 for(const name of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
  const glyphs=new Set();for(const spec of fonts[name].charset.split(',')){const [a,b=a]=spec.split('-').map(Number);for(let n=a;n<=b;n++)glyphs.add(n);}
  for(const character of names)assert(glyphs.has(character.codePointAt(0)),`${name} missing ${character}`);
 }
});
