import test from 'node:test';
import assert from 'node:assert/strict';
import {writeFileSync,mkdtempSync,rmSync,readFileSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';
const root=new URL('..',import.meta.url).pathname;
function run(source){const dir=mkdtempSync(join(tmpdir(),'elim-fx-'));try{
 writeFileSync(join(dir,'test.cpp'),source);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});
 execFileSync(join(dir,'test'),[],{stdio:'pipe'});
 }catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('elimination effects: one 1st executes all, two split by LP then name, three or more play default',()=>run(`
#include "domain/SurvivalEliminationEffects.hpp"
#include <cassert>
using namespace corum::ranked;
int main(){
 std::vector<std::string> order{"v1","v2","v3","v4","v5"};
 std::vector<SurvivalEffectPlayer> one{{"a","Mika",1,1820,EliminationEffect::Slice},{"b","Arc",2,1650,EliminationEffect::Bolt}};
 auto e=survivalEliminationExecutions(one,order);assert(e.size()==5);
 for(auto const& x:e){assert(x.executorId=="a");assert(x.effect==EliminationEffect::Slice);}
 // Two tied: the higher LP takes the odd one (3 of 5), alternating in order.
 std::vector<SurvivalEffectPlayer> two{{"b","Arcturus",1,1650,EliminationEffect::Bolt},{"a","Mika",1,1820,EliminationEffect::Slice}};
 e=survivalEliminationExecutions(two,order);
 assert(e[0].executorId=="a"&&e[1].executorId=="b"&&e[2].executorId=="a"&&e[3].executorId=="b"&&e[4].executorId=="a");
 assert(e[1].effect==EliminationEffect::Bolt&&e[0].effect==EliminationEffect::Slice);
 // Same LP: name A-Z, case-insensitive.
 std::vector<SurvivalEffectPlayer> same{{"m","mika",1,1700,EliminationEffect::Slice},{"z","Arcturus",1,1700,EliminationEffect::Vortex}};
 e=survivalEliminationExecutions(same,{"v1","v2","v3"});
 assert(e[0].executorId=="z"&&e[1].executorId=="m"&&e[2].executorId=="z");
 std::vector<SurvivalEffectPlayer> three{{"a","A",1,1,EliminationEffect::Slice},{"b","B",1,1,EliminationEffect::Slice},{"c","C",1,1,EliminationEffect::Slice}};
 for(auto const& x:survivalEliminationExecutions(three,order)){assert(x.executorId.empty());assert(x.effect==EliminationEffect::Default);}
 assert(survivalEliminationExecutions({},order)[0].executorId.empty());
 assert(eliminationEffectFromCode("VORTEX")==EliminationEffect::Vortex);assert(eliminationEffectFromCode("??")==EliminationEffect::Default);
 assert(eliminationEffectCode(EliminationEffect::Shatter)=="SHATTER");
 assert(eliminationRequirement(EliminationEffect::Slice).key=="elim-req-both"&&eliminationRequirement(EliminationEffect::Slice).tier=="GOLD");
 assert(eliminationRequirement(EliminationEffect::Bolt).key=="elim-req-survival"&&eliminationRequirement(EliminationEffect::Bolt).tier=="SILVER");
 auto g=achievementApGrade(1240);assert(g.tier=="BRONZE"&&g.floorAp==1000&&g.nextTier=="SILVER"&&g.nextAp==2000);
 g=achievementApGrade(0);assert(g.tier.empty()&&g.nextTier=="RED"&&g.nextAp==100);
 g=achievementApGrade(4965);assert(g.tier=="GOLD"&&g.nextAp==0);
}
`));

test('elimination effects: client mirrors the rules catalog, unlocks and grades',()=>{
 const rules=readFileSync(join(root,'../ranked/packages/rules/src/elimination-effects.ts'),'utf8');
 const domain=readFileSync(join(root,'src/domain/SurvivalEliminationEffects.hpp'),'utf8');
 assert.match(rules,/ELIMINATION_EFFECTS = \["DEFAULT", "BOLT", "SLICE", "SHATTER", "VORTEX"\]/);
 assert.match(rules,/BOLT: \{ kind: "SURVIVAL_TIER", tier: "SILVER" \}/);
 assert.match(rules,/SLICE: \{ kind: "SURVIVAL_AND_SOLO_TIER", tier: "GOLD" \}/);
 for(const [tier,ap] of [['RED',100],['AQUA',400],['BRONZE',1000],['SILVER',2000],['GOLD',3200]]){
  assert.match(rules,new RegExp(`\\{ tier: "${tier}", minAp: ${ap} \\}`));assert.match(domain,new RegExp(`\\{"${tier}",${ap}\\}`));}
});

test('elimination reveal: gold 1st card, executions from the completed round, custom effects inside the window',()=>{
 const layer=readFileSync(join(root,'src/ui/SurvivalGameLayer.cpp'),'utf8');
 const fx=readFileSync(join(root,'src/ui/SurvivalEliminationFx.cpp'),'utf8');
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 assert.match(layer,/auto const goldCard=completed && value\.rank==1 && value\.passing;/);
 assert.match(layer,/tile\.goldBackground->setVisible\(goldCard\)/);
 assert.match(layer,/m_eliminationExecutions=survivalEliminationExecutions\(effectPlayers,m_eliminationOrder\);/);
 assert.match(layer,/p\["eliminationEffect"\]/);assert.match(layer,/p\["lp"\]/);
 assert.match(layer,/execution\.effect!=EliminationEffect::Default && execution\.effect!=EliminationEffect::Slice && !execution\.executorId\.empty\(\)\)\{playExecution/);
 assert.match(fx,/float eliminationFxSeconds\(\) \{ return SurvivalEliminationFallMilliseconds \/ 1000\.0f; \}/);
 assert.doesNotMatch(fx,/drawDot|drawSegment/);
 assert.match(popup,/addEliminationFxButton\(\{23,18\}\);/);
 assert.match(popup,/if \(m_eliminationFx\) if \(auto popup = m_eliminationFx->lock\(\)\) popup->dismiss\(\);/);
 assert.match(popup,/auto const grade = corum::ranked::achievementApGrade\(totalAp\);/);
});

test('achievement grade emblem opens the ladder; effect picker previews the selected effect',()=>{
 const popup=readFileSync(join(root,'src/RankedPopup.cpp'),'utf8');
 const picker=readFileSync(join(root,'src/ui/RankedEliminationFxPopup.cpp'),'utf8');
 const ladder=readFileSync(join(root,'src/ui/RankedAchievementGradePopup.hpp'),'utf8');
 const frame=readFileSync(join(root,'src/ui/AchievementCardFrame.cpp'),'utf8');
 const frameHeader=readFileSync(join(root,'src/ui/AchievementCardFrame.hpp'),'utf8');
 const layer=readFileSync(join(root,'src/ui/SurvivalGameLayer.cpp'),'utf8');
 assert.match(popup,/CCMenuItemSpriteExtra::create\(ap, this, menu_selector\(CorumRankedLayer::onAchievementGrade\)\)/);
 assert.match(popup,/achievementGradeEmblem\(21\.0f, gradeTone, gradePanel\)/);
 assert.match(popup,/RankedAchievementGradePopup::create\(RankedRuntime::get\(\)\.view\(\)\.achievements\.totalAp\)/);
 assert.match(ladder,/for \(std::size_t i = 0; i < kAchievementApGrades\.size\(\); \+\+i\)/);
 // Border between the first F frame (1.5) and the thin draft (0.8).
 assert.match(frameHeader,/inline constexpr float kAchievementFrameWidth = 1\.15f;/);
 assert.match(frame,/frame\(m_static, m_size, 0, kAchievementFrameWidth,/);
 // Preview: rows select (locked ones too), the stage replays every 2.6 s, the game and the preview share the motion.
 assert.match(picker,/menu_selector\(RankedEliminationFxPopup::onSelect\)/);
 assert.match(picker,/schedule\(schedule_selector\(RankedEliminationFxPopup::previewTick\), kPreviewLoop\)/);assert.match(picker,/constexpr float kPreviewLoop = 3\.0f;/);
 assert.match(picker,/survivalEliminationStarts\(m_victims\.size\(\)\)/);
 // Every victim owns its cut: moving the ghost on to the next victim never cancels it.
 // Slice "gather and cut" (game and preview share runSliceGathering).
 assert.match(picker,/runSliceGathering\(m_world, executor, victims, \{size\.width \* 0\.62f, size\.height \* 0\.55f\}, fall, m_world\);/);assert.match(picker,/executor\.slot = m_executorIcon;/);
 assert.match(layer,/ui::runSliceGathering\(this,executor,victims,\{pileX,size\.height\*\.46f\},size\.height\*\.6f,m_grid\);/);
 // Held cards turn red only when their icon leaves for the pile.
 assert.match(layer,/if\(m_sliceHeld\.contains\(id\)\)return;/);
 assert.match(layer,/owner->m_sliceHeld\.erase\(id\);owner->m_eliminationRevealed\.insert\(id\);/);
 assert.match(layer,/if\(!m_slicePlanned\.insert\(executorId\)\.second\)return;/);
 assert.match(layer,/m_slicePlanned\.clear\(\);m_sliceHeld\.clear\(\);/);
 // Two tied executors: a pile each.
 assert.match(layer,/pileX=executors\.front\(\)==executorId\?size\.width\*\.36f:size\.width\*\.64f;/);
 const fx=readFileSync(join(root,'src/ui/SurvivalEliminationFx.cpp'),'utf8');
 const gather=fx.slice(fx.indexOf('void runSliceGathering'),fx.indexOf('void playEliminationFx'));
 // Gold executor, departures over 0.55 s, first sweep at 1.3 s, about five cuts per big sweep
 // (at most six sweeps), a final swing after a 0.3 s beat, afterimages, shakes and a finale burst.
 assert.match(gather,/player->setGold\(true\)/);assert.match(fx,/constexpr float kGatherSpread = 0\.55f;/);assert.match(fx,/constexpr float kGatherCutStart = 1\.3f;/);
 assert.match(fx,/plan\.sweeps = regular == 0 \? 0 : std::min<std::size_t>\(6, \(regular \+ 4\) \/ 5\);/);
 assert.match(fx,/constexpr float kGatherFinalBeat = 0\.30f;/);assert.match(fx,/constexpr float kGatherBudget = 3\.45f;/);
 assert.match(fx,/CCMoveTo::create\(kGatherReturn, executor\.home\)/);
 assert.match(gather,/player->setIconOpacity\(opacity\)/);assert.match(gather,/finaleBurst\(stage, target\.center\)/);
 assert.match(gather,/shakeNode\(node\.data\(\), shakeBase, 10\.0f, 7\)/);
 // Simplified bolt: short (46 before icon scale), thin, four sparks, no scale-up bump.
 assert.match(fx,/auto const top = 46\.0f;/);assert.match(fx,/sparks\(target\.host, target\.center, 4,/);assert.doesNotMatch(fx,/CCScaleTo::create\(0\.1f, 1\.25f\)/);
 // Season note under the list; the both-modes requirement wording.
 assert.match(picker,/elim-fx-season-note/);
 assert.match(layer,/ui::playDefaultFall\(tile\.icon,static_cast<int>\(order\),getContentSize\(\)\.height\);/);
});

test('Slice gathering for a 64-player round fits the elimination window plus the hold',()=>{
 // Mirrors gatherPlan: sweeps = min(6, ceil((n-1)/5)), sweep = min(0.26, (3.45-1.3-0.3-0.5)/sweeps).
 const total=(n)=>{const regular=n-1;const sweeps=regular===0?0:Math.min(6,Math.ceil(regular/5));
  const sweep=sweeps?Math.min(0.26,(3.45-1.3-0.3-0.15-0.35)/sweeps):0.26;return 1.3+sweeps*sweep+0.3+0.15+0.35;};
 assert.ok(Math.abs(total(24)-3.4)<1e-9);
 for(const n of [1,2,3,8,24,40,48,63])assert.ok(total(n)<=1.55+2.0+1e-9,`${n} eliminated`);
});
