import test from 'node:test';
import assert from 'node:assert/strict';
import {writeFileSync,mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
function run(source){const dir=mkdtempSync(join(tmpdir(),'tier-progress-'));try{
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}
test('tier bar: band progress, near, warning, promotion, GOLD rank chase, ladder leader and top band',()=>run(`
#include "domain/TierProgress.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 TierProgressInput in;in.scoreKnown=true;in.bands={{"SILVER3",600},{"SILVER2",634},{"SILVER1",667},{"GOLD",700}};
 in.tier="SILVER3";in.score=620;
 auto v=tierProgress(in);assert(v.kind==TierProgressKind::Progress&&v.nextTier=="SILVER2"&&v.remaining==14&&v.maximum==634);
 in.score=632;assert(tierProgress(in).kind==TierProgressKind::Near);
 in.score=600;in.demotionWarning=true;assert(tierProgress(in).kind==TierProgressKind::Warning);in.demotionWarning=false;
 in.promotionActive=true;in.promotionGames=3;in.promotionResults={"WIN"};v=tierProgress(in);
 assert(v.kind==TierProgressKind::Promotion&&v.promotionGames==3&&v.fill==1.0f);in.promotionActive=false;
 in.tier="MASTER";in.ladderTier=true;in.score=760;in.rank=12;in.target=StandingTargetView{1,"above",false,11,790};
 v=tierProgress(in);assert(v.kind==TierProgressKind::RankChase&&v.targetRank==11&&v.remaining==30&&v.minimum==700&&v.maximum==790);
 in.target->score=760;assert(tierProgress(in).remaining==1);
 in.rank=1;in.target.reset();v=tierProgress(in);assert(v.kind==TierProgressKind::Leader&&v.fill==1.0f);
 in.rank=5;assert(tierProgress(in).kind==TierProgressKind::Top);
 in.ladderTier=false;in.tier="GOLD";in.rank.reset();assert(tierProgress(in).kind==TierProgressKind::Top);
 in.scoreKnown=false;assert(tierProgress(in).kind==TierProgressKind::Hidden);
}
`));
test('tier bar is wired under both start buttons and fades with the panel',()=>{
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8'),bar=readFileSync(join(root,'src/ui/TierProgressBar.cpp'),'utf8');
 assert.match(popup,/addTierProgress\(view, false, \{rightCenter\.x, rightCenter\.y - 80\.0f\}\)/);
 assert.match(popup,/addTierProgress\(view, true, \{94, -80\.0f\}\)/);
 assert.match(bar,/getDisplayedOpacity\(\)/);assert.match(bar,/achievementPastelAt\(position, seconds\)/);assert.match(bar,/RankBadgeDisplay::Matched/);
});
test('Survival promotion plays its tier motion first: promotion -> podium -> score -> result',()=>{
 const layer=readFileSync(join(root,'src/ui/SurvivalGameLayer.cpp'),'utf8'),runtime=readFileSync(join(root,'src/SurvivalRuntime.cpp'),'utf8');
 assert.match(layer,/stage==SurvivalFlowStage::Ceremony\|\|stage==SurvivalFlowStage::ScoreAnimation\) && runtime\.promotionMotionPending\(\)\)beginPromotionMotion\(\)/);
 assert.match(layer,/!\(input->promoted && runtime\.promotionMotionShown\(\)\)/);
 assert.match(runtime,/resultFlowKey\(\)\+":promotion"/);
});
test('lobby animates any LP change since the last visit: digit roll, tier bar, emblem hand-over',()=>{
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8'),motion=readFileSync(join(root,'src/ui/LobbyScoreTransition.cpp'),'utf8');
 assert.match(popup,/readSeen\("card", survival\)/);assert.match(popup,/writeSeen\("bar", survival, nowTier, nowLp\)/);
 assert.match(popup,/RollingNumber::create\(seen\.lp, view\.profileScore, "LP"/);
 assert.match(popup,/LobbyTierTransition::create\(group, oldEmblem, emblem, oldTier, tier, bloom, up\)/);
 assert.match(popup,/bar->animateFrom\(std::move\(from\), up\)/);
 // Lobby nodes are driven per frame: no CCEase/CCFade actions on them.
 assert.doesNotMatch(motion,/CCEase|CCFadeTo|runAction/);
 // Faded-out nodes leave the card, so a later lobby fade cannot bring them back.
 assert.match(motion,/removeFromParentAndCleanup\(true\)/);
});
