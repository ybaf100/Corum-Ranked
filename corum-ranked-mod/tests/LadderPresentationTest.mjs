import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
const read=p=>readFileSync(join(root,p),'utf8');
const method=(src,sig)=>{const at=src.indexOf(sig);assert(at>=0,sig);let depth=0,i=src.indexOf('{',at);for(;i<src.length;i++){if(src[i]==='{')depth++;else if(src[i]==='}'&&--depth===0)break;}return src.slice(at,i+1);};

test('Survival GOLD result: own icon and rank on a full bar, as Solo #1 (standing from the server result)',()=>{
 const runtime=read('src/SurvivalRuntime.cpp');
 assert.match(runtime,/input\.standingBefore=parseStandingView\(result\["standingBefore"\]\);input\.standingAfter=parseStandingView\(result\["standingAfter"\]\);/);
 assert.match(runtime,/if\(input\.standingAfter\.rank\)input\.standingAfter\.displayTier=input\.afterTier;/);
 const dir=mkdtempSync(join(tmpdir(),'corum-ladder-result-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/ResultScorePresentation.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 ScoreAnimationInput input;input.matchId="m";input.beforeTier=input.afterTier="GOLD";input.before=1500;input.after=1540;input.survivalRank=1;
 input.bands={{"RED",0},{"AQUA",300},{"BRONZE",600},{"SILVER",900},{"GOLD",1200}};
 auto ladder=[](int rank){StandingView s;s.rank=rank;s.total=40;s.displayTier="GOLD";return s;};
 // #1 and any rank without a player above (Survival has no targets): own icon, own rank, full bar.
 for(int rank:{1,4}){
  input.standingBefore=ladder(rank+1);input.standingAfter=ladder(rank);
  auto const start=sampleScoreProgress(input,0.0),end=sampleScoreProgress(input,input.duration());
  assert(start.goldLeader&&start.fill==1.0&&*start.standing.rank==rank+1);
  assert(end.goldLeader&&end.fill==1.0&&*end.standing.rank==rank&&end.nextTier.empty());
 }
 // A ladder with a player above keeps chasing them (Solo).
 input.standingBefore=input.standingAfter=ladder(3);input.standingAfter.target=StandingTargetView{};input.standingAfter.target->rank=2;input.standingAfter.target->score=1600;
 input.standingBefore.target=input.standingAfter.target;
 assert(!sampleScoreProgress(input,0.0).goldLeader);
 // No standing (an older server): unchanged, no own-rank display.
 input.standingBefore=input.standingAfter=StandingView{};
 assert(!sampleScoreProgress(input,input.duration()).goldLeader);
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
});

test('promotion series: the season ladder bar shines in the tier colour and shows target, requirement and marks',()=>{
 const popup=read('src/RankedPopup.cpp');
 const panel=method(popup,'    void renderModePanel(RuntimeView const& view, corum::ranked::RankedMode mode) {');
 // Survival reads its own series, not the Solo one.
 assert.match(panel,/auto const& series = survival \? view\.survival\.profile\.promotion : view\.promotion;/);
 assert.match(panel,/LadderShineBar::create\(\{ladderX, ladderTop - 19\.0f\}, ladderW, px\(12\.0f\), tierColor\(nowTier\)\)/);
 assert.match(panel,/tr\("promotion-short", "PROMOTION"\), upper\(series\.targetTier\)/);
 assert.match(panel,/auto const nextMark = promotion \? upper\(series\.targetTier\) : progress\.nextTier;/);
 assert.match(panel,/addSeriesChips\(m_root, \{ladderX, ladderTop - 36\.0f\}, corum::ranked::seriesMarks\(series\.gamesRequired,/);
 const bar=read('src/ui/LadderShineBar.hpp');
 // Fades with the lobby panel: every piece cascades the parent's opacity.
 assert.match(bar,/bar->setCascadeOpacityEnabled\(true\);/);assert.match(bar,/layer->setCascadeOpacityEnabled\(true\);/);
});

test('the top-left back button wins over an overlapping tap area (the Survival half on the mode select)',()=>{
 const back=method(read('src/RankedPopup.cpp'),'    void addTopBack(bool closeLayer = true) {');
 assert.match(back,/menu->setTouchPriority\(kCCMenuHandlerPriority - 1\);/);
 assert.match(back,/menu->addChild\(button\);\s*m_root->addChild\(menu, 30\);/);
 assert.doesNotMatch(back,/m_menu->addChild\(button\)/);
});

test('Solo promotion slots: the games still needed as wins carry a green outline everywhere the series shows',()=>{
 const dir=mkdtempSync(join(tmpdir(),'corum-series-marks-'));
 try{
  writeFileSync(join(dir,'test.cpp'),`#include "domain/PromotionMarks.hpp"
#include <cassert>
#include <string>
using namespace corum::ranked;
// W = win, L = loss, G = needed win (green outline), - = open.
std::string show(int games,int wins,std::vector<std::string> const& results){
 std::string out;for(auto mark:seriesMarks(games,wins,results))out+=mark==SeriesMark::Win?'W':mark==SeriesMark::Loss?'L':mark==SeriesMark::NeedWin?'G':'-';return out;
}
int main(){
 // 3 wins of 5.
 assert(show(5,3,{"LOSS","LOSS"})=="LLGGG");
 assert(show(5,3,{"LOSS","WIN","LOSS"})=="LWLGG");
 assert(show(5,3,{"WIN","WIN","LOSS"})=="WWLG-");
 assert(show(5,3,{})=="GGG--");
 // 2 wins of 3.
 assert(show(3,2,{"WIN"})=="WG-");
 assert(show(3,2,{})=="GG-");
 // No win count (Survival cutoff series): nothing outlined.
 assert(show(3,0,{"LOSS"})=="L--");
}
`);
  execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
  execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }finally{rmSync(dir,{recursive:true,force:true});}
 const popup=read('src/RankedPopup.cpp');
 // The mode card and the mode panel both draw the slots through seriesMarks; Survival passes no win count.
 assert.equal([...popup.matchAll(/addSeriesChips\(m_root, \{[^}]*\}, corum::ranked::seriesMarks\(/g)].length,2);
 assert.equal([...popup.matchAll(/survival \? 0 : (?:view\.promotion|series)\.winsRequired/g)].length,2);
 assert.match(popup,/input\.promotionWins = view\.promotion\.winsRequired;/);
 assert.match(read('src/ui/TierProgressBar.cpp'),/seriesMarks\(view\.promotionGames, view\.promotionWins, view\.promotionResults\)/);
});

test('during a promotion series the LP shows in the gold title shimmer',()=>{
 const card=method(read('src/RankedPopup.cpp'),'    void renderModeCard(RuntimeView const& source, corum::ranked::RankedMode mode, CCLayerColor* group) {');
 const at=card.indexOf('auto const addLp = [&](CCPoint at, float scale, CCPoint anchor) {');assert(at>=0);
 const lp=card.slice(at,card.indexOf('};',at));
 const gold=lp.indexOf('if (view.promotion.active) {');
 assert(gold>=0&&lp.indexOf('styleAchievementText(lp, 4); m_root->addChild(lp, 6); return;',gold)>gold);
 // Before the rolling number: one label, so the sweep crosses the whole value.
 assert(lp.indexOf('view.promotion.active')<lp.indexOf('RollingNumber::create'));
 // Survival's card reads the Survival series.
 assert.match(card,/view\.promotion = profile\.promotion;/);
});
