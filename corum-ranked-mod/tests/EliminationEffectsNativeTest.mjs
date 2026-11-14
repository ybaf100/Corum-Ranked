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
 assert.match(popup,/menu_selector\(CorumRankedLayer::onEliminationFx\), "survival-elimination-fx-button"/);
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
 assert.match(popup,/achievementGradeEmblem\(21\.0f, grade\.tier, gradeTone, gradePanel\)/);
 const grades=ladder;
 // Grade emblems are the tier's rank core; the artwork is already a 45-degree square, so it is not rotated again.
 assert.match(grades,/rankResourceSprite\("rank-" \+ name \+ "-core\.png"\)/);assert.match(grades,/core->setRotation\(0\.0f\)/);
 // The grade ladder shows each grade's tier core only, not the full tier mark.
 assert.match(grades,/achievementGradeEmblem\(20\.0f, name, rowTone, achievementGradePanel\(name\)\)/);
 assert.doesNotMatch(grades,/createAnimatedRankBadge/);
 // The grade plate is a button face that stays upright (no slab lean). The
 // mark is a user flag: CCMenuItemSprite re-tags its image, so a tag is lost
 // as soon as the plate becomes the button's face (beta.70 shipped that way).
 assert.match(popup,/auto\* ap = corum::ranked::ui::achievementPlate\([^\n]*\n(?:\s*\/\/[^\n]*\n)*\s*corum::ranked::ui::keepUpright\(ap\);/);
 const buttonFx=readFileSync(join(root,'src/ui/ButtonFx.hpp'),'utf8');
 assert.match(buttonFx,/setUserFlag\("upright-face"_spr\)/);assert.match(buttonFx,/!keepsUpright\(face\) &&/);
 assert.doesNotMatch(buttonFx,/getTag\(\) != Upright/);
 assert.match(popup,/RankedAchievementGradePopup::create\(RankedRuntime::get\(\)\.view\(\)\.achievements\.totalAp\)/);
 assert.match(ladder,/for \(std::size_t i = 0; i < kAchievementApGrades\.size\(\); \+\+i\)/);
 // Border between the first F frame (1.5) and the thin draft (0.8).
 assert.match(frameHeader,/inline constexpr float kAchievementFrameWidth = 1\.15f;/);
 assert.match(frame,/frame\(m_static, m_size, 0, kAchievementFrameWidth,/);
 // Preview: rows select (locked ones too), the stage replays every 2.6 s, the game and the preview share the motion.
 assert.match(picker,/menu_selector\(RankedEliminationFxPopup::onSelect\)/);
 assert.match(picker,/schedule\(schedule_selector\(RankedEliminationFxPopup::previewTick\), previewLoop\(m_selected\)\)/);assert.match(picker,/constexpr float kPreviewLoop = 3\.0f;/);
 assert.match(picker,/kPreviewLead \+ sliceGatherSeconds\(3\) \+ 0\.9f/);
 assert.match(picker,/survivalEliminationStarts\(m_victims\.size\(\)\)/);
 // Every victim owns its cut: moving the ghost on to the next victim never cancels it.
 // Slice "musou" (game and preview share runSliceGathering): a scatter field, not a pile.
 assert.match(picker,/runSliceGathering\(m_world, \{executor\}, victims, \{size\.width \* 0\.56f, size\.height \* 0\.5f\}, \{size\.width \* 0\.3f, size\.height \* 0\.2f\}, fall, m_world\);/);assert.match(picker,/executor\.slot = m_executorIcon;/);
 assert.match(layer,/ui::runSliceGathering\(this,executors,victims,\{size\.width\/2,size\.height\*\.45f\},CCSize\{size\.width\*\.31f,size\.height\*\.24f\},size\.height\*\.6f,m_grid\);/);
 // Held cards turn red only when their icon leaves for the field.
 assert.match(layer,/if\(m_sliceHeld\.contains\(id\)\)return;/);
 assert.match(layer,/owner->m_sliceHeld\.erase\(id\);owner->m_eliminationRevealed\.insert\(id\);/);
 assert.match(layer,/if\(m_sliceStarted\)return;\n\s*m_sliceStarted=true;/);
 assert.match(layer,/m_sliceStarted=false;m_sliceHeld\.clear\(\);/);
 // Mixed tie (one Slice, one other effect): the other effect plays first, Slice cards are held, then Slice starts.
 assert.match(layer,/if\(m_sliceDelayMs>0\)\{m_sliceHeld\.insert\(id\);return;\}/);
 assert.match(layer,/if\(m_sliceDelayMs>0 && !m_sliceStarted && elapsed>=m_sliceDelayMs && !m_sliceHeld\.empty\(\)\)startSliceGathering\(\);/);
 assert.match(layer,/if\(m_sliceHeld\.erase\(id\)\)revealElimination\(id,false,i\);/);
 // Two tied executors both on Slice: one joint field (alternating dashes, one counter, an X finale).
 assert.match(layer,/executors\.push_back\(std::move\(executor\)\);/);
 const fx=readFileSync(join(root,'src/ui/SurvivalEliminationFx.cpp'),'utf8');
 const gather=fx.slice(fx.indexOf('void runSliceGathering'),fx.indexOf('void playEliminationFx'));
 // Gold executor, one flash-like dash per victim in the planned order, the finale
 // (wind-up, strike, hit-stop, through, follow) and the flight home.
 assert.match(gather,/player->setGold\(true\)/);assert.match(gather,/sliceMusouPlan\(n, joint,/);
 assert.match(gather,/for \(std::size_t e = 0; e < joint; \+\+e\)/);assert.match(gather,/if \(cross\) slashLine\(stage, target\.center, \*cross, crossLength\);/);
 assert.match(gather,/std::stable_sort\(byCut\.begin\(\), byCut\.end\(\)/);
 assert.match(fx,/for \(auto const& dash : plan\.dashes\)/);
 assert.match(fx,/CCEaseExponentialOut::create\(CCMoveTo::create\(plan\.dash, point\(dash\.to\)\)\)/);
 // Long-exposure light trail behind the flash dashes (short: 0.12 s), no afterimage copies.
 assert.match(gather,/LightTrail::create\(ghost, 0\.12f, iconSize \* 0\.6f\)/);assert.doesNotMatch(gather,/setIconOpacity/);
 assert.match(fx,/class LightTrail final : public CCNode/);assert.match(fx,/m_draw->drawPolygon\(quad, 4, color, 0, color\);/);
 assert.match(fx,/actions->addObject\(CCDelayTime::create\(SliceHitStop\)\);/);
 assert.match(fx,/back\.endPosition = executor\.home;/);
 assert.match(gather,/finaleBurst\(stage, target\.center, std::max\(0\.7f, fieldScale\)\)/);
 // Shake on every cut, perpendicular to the blade (board and stage), harder every fifth, hard on the finale.
 assert.match(gather,/auto const amplitude = strong \? 4\.5f : 2\.5f;/);assert.match(gather,/auto const strong = count % 5 == 0;/);
 assert.match(gather,/shakeNode\(node\.data\(\), shakeBase, push, 10\.0f, 8\)/);assert.match(gather,/shakeNode\(node\.data\(\), stageBase, push, 8\.0f, 8\)/);
 // Simplified bolt: short (46 before icon scale), thin, four sparks, no scale-up bump.
 assert.match(fx,/auto const top = 46\.0f;/);assert.match(fx,/sparks\(target\.host, target\.center, 4,/);assert.doesNotMatch(fx,/CCScaleTo::create\(0\.1f, 1\.25f\)/);
 // Season note under the list; the both-modes requirement wording.
 assert.match(picker,/elim-fx-season-note/);
 assert.match(layer,/ui::playDefaultFall\(tile\.icon,static_cast<int>\(order\),getContentSize\(\)\.height\);/);
});

test('Slice musou plan: scattered field, one cut per dash in all directions, finale, home; result waits for it',()=>run(`
#include "domain/SurvivalSlicePlan.hpp"
#include <cassert>
#include <cmath>
#include <set>
using namespace corum::ranked;
int main(){
 // Same formula as the rules package survivalSliceMs / survivalResultMs.
 assert(sliceMusouMilliseconds(0)==0 && sliceMusouMilliseconds(1)==2820);
 assert(sliceMusouMilliseconds(8)==1300+7*100+1520 && sliceMusouMilliseconds(24)==1300+23*87+1520 && sliceMusouMilliseconds(48)==1300+47*50+1520);
 assert(sliceResultMilliseconds(24)==7871 && sliceResultMilliseconds(0)==5500 && sliceResultMilliseconds(1)==1950+2820+1100);
 for(std::size_t n:{1u,2u,3u,8u,24u,40u,48u,63u}){
  auto const plan=sliceMusouPlan(n,1,{285,145},{176,77},40,30,120,{26,26},{543,294});
  assert(plan.field.size()==n && plan.leave.size()==n && plan.dashes.size()==n-1);
  std::set<std::size_t> cut{plan.finale};float last=0;
  for(auto const& d:plan.dashes){assert(cut.insert(d.victim).second);assert(d.cut>=d.start && d.cut<=d.start+plan.dash+1e-6f);assert(d.cut>last);last=d.cut;}
  assert(cut.size()==n);
  assert(plan.finaleCut>last && plan.windupAt>=last);
  assert(std::abs(plan.home-sliceMusouMilliseconds(n)/1000.0f)<0.002f);
  // Every victim has landed before the first dash.
  for(auto t:plan.leave)assert(t+SliceVictimFlight<=SliceCutStartMs/1000.0f+1e-6f);
  for(auto p:plan.field)assert(std::pow((p.x-285)/176,2)+std::pow((p.y-145)/77,2)<=1.0001);
  assert(plan.windup[0].x>=26 && plan.windup[0].x<=543 && plan.through[0].y>=26 && plan.through[0].y<=294);
 }
 // Joint cut (two tied Slice executors): same rules timing, alternating picks, each executor's
 // dashes back to back (the second half a dash later), both strike the last victim from an X.
 assert(sliceJointMilliseconds(0)==0 && sliceJointMilliseconds(1)==2820);
 assert(sliceJointMilliseconds(24)==1300+12*100+50+1520 && sliceJointMilliseconds(48)==1300+24*83+41+1520);
 for(std::size_t n:{1u,2u,3u,8u,24u,40u,48u,63u}){
  auto const plan=sliceMusouPlan(n,2,{285,145},{176,77},40,30,120,{26,26},{543,294});
  assert(plan.start.size()==2 && plan.windup.size()==2 && plan.through.size()==2 && plan.finaleDir.size()==2);
  assert(plan.dashes.size()==n-1);
  std::set<std::size_t> cut{plan.finale};float last[2]={0,0};std::size_t count[2]={0,0};
  for(auto const& d:plan.dashes){assert(cut.insert(d.victim).second);assert(d.executor<2);assert(d.start>=last[d.executor]-1e-5f);
   last[d.executor]=d.start+plan.dash;++count[d.executor];}
  assert(cut.size()==n && count[0]>=count[1] && count[0]-count[1]<=1);
  assert(plan.windupAt+1e-5f>=last[0] && plan.windupAt+1e-5f>=last[1]);
  assert(std::abs(plan.home-sliceJointMilliseconds(n)/1000.0f)<0.002f);
  // An X: opposite horizontal sides, both from above.
  auto const t=plan.field[plan.finale];
  assert(plan.windup[0].x<=t.x && plan.windup[1].x>=t.x);
  assert(plan.finaleDir[0].x*plan.finaleDir[1].x<=0.0001f);
 }
 // Joint is shorter than one executor cutting everyone.
 assert(sliceJointMilliseconds(24)<sliceMusouMilliseconds(24));
 assert(sliceResultFromAnimation(SliceAfterOthersMs+sliceMusouMilliseconds(12))==1950+1550+sliceMusouMilliseconds(12)+1100);
 // All directions: consecutive dashes turn (most turn by more than 60 degrees).
 auto const plan=sliceMusouPlan(24,1,{640,405},{380,205},55,42,170,{40,40},{1240,680});
 int hard=0;for(std::size_t j=1;j<plan.dashes.size();++j){auto a=plan.dashes[j-1].dir,b=plan.dashes[j].dir;if(a.x*b.x+a.y*b.y<0.5f)++hard;}
 assert(hard*3>=static_cast<int>(plan.dashes.size()-1)*2);
 // Deterministic for the same count.
 auto const again=sliceMusouPlan(24,1,{640,405},{380,205},55,42,170,{40,40},{1240,680});
 assert(again.finale==plan.finale && again.dashes.front().victim==plan.dashes.front().victim);
}`));

test('result presentation waits until the Slice icon is home',()=>{
 const layer=readFileSync(join(root,'src/ui/SurvivalGameLayer.cpp'),'utf8');
 assert.match(layer,/if\(slices\.size\(\)>1\)hold\.animation=sliceJointMilliseconds\(slice\);/);
 assert.match(layer,/hold\.sliceDelay=others>0\?SliceAfterOthersMs:0;/);assert.match(layer,/hold\.animation=hold\.sliceDelay\+sliceMusouMilliseconds\(slice\);/);
 assert.match(layer,/auto const hold=eliminationHold\(completed\);m_sliceDelayMs=hold\.sliceDelay;/);
 assert.match(layer,/auto const server=static_cast<int>\(completed\["presentationMs"\]\.asInt\(\)\.unwrapOr\(0\)\);/);
 assert.match(layer,/m_eliminationUntil=now\+std::chrono::milliseconds\(alreadyPresented\?0:SurvivalBeforeEliminationMilliseconds\+std::max\(SurvivalEliminationMilliseconds,hold\.animation\)\);/);
 assert.match(layer,/m_nextMapAt=now\+std::chrono::milliseconds\(alreadyPresented\?SurvivalRosterMilliseconds:hold\.total\);/);
 const rules=readFileSync(join(root,'../ranked/packages/rules/src/survival-match.ts'),'utf8');
 assert.match(rules,/return 1300\+regular\*dash\+1520;/);assert.match(rules,/Math\.min\(100,Math\.max\(50,Math\.round\(2000\/regular\)\)\)/);
 assert.match(rules,/export const SURVIVAL_SLICE_HOLD_MS = 1_100;/);assert.match(rules,/export const SURVIVAL_SLICE_AFTER_OTHERS_MS = 1_550;/);
 assert.match(rules,/return 1300\+per\*dash\+Math\.floor\(dash\/2\)\+1520;/);assert.match(rules,/presentationMs:survivalResultMs\(match,completed\)/);
});

test('Slice reads on compact 64-player cards: backdrop, enlarged icons, combo counter, sharp hits, sound',()=>{
 const fx=readFileSync(join(root,'src/ui/SurvivalEliminationFx.cpp'),'utf8');
 // The scattered icons and the gold icon grow off the cards.
 assert.match(fx,/auto const fieldScale = std::clamp\(executor\.iconScale \* 2\.0f, std::max\(executor\.iconScale, 0\.45f\), 1\.0f\);/);
 assert.match(fx,/CCScaleTo::create\(SliceVictimFlight, fieldScale\)/);
 // A dark backdrop fades in after the scatter and lifts during the flight home.
 assert.match(fx,/CCFadeTo::create\(0\.45f, 160\)/);
 // x1, x2, ... at the field's top right, a bigger white punch on the last cut.
 assert.match(fx,/counter->setString\(fmt::format\("x\{\}", count\)\.c_str\(\)\);/);
 // Small icon-sized hit flash and a hairline slash.
 assert.match(fx,/flash\(target\.host, target\.center, \(finale \? 14\.0f : 8\.0f\) \* std::max\(0\.6f, target\.iconScale\), \{1, 1, 1, 0\.75f\}\);/);
 assert.match(fx,/Layer\{3\.6f, \{255, 211, 78, 120\}\}, Layer\{1\.4f, \{255, 255, 255, 255\}\}/);
 // A crash sound on every cut (alternating pitch), heavier on the finale, at the Ranked volume.
 assert.match(fx,/playSlashSound\(count % 2 \? 1\.3f : 1\.45f, strong \? 0\.14f : 0\.08f\);/);assert.match(fx,/playSlashSound\(0\.9f, 0\.28f\);/);
 assert.match(fx,/RankedVolumeControl::get\(\)\.volume\(\)/);
});
