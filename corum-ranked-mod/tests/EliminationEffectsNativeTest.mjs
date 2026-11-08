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
 assert.match(layer,/execution\.effect!=EliminationEffect::Default && !execution\.executorId\.empty\(\)\)\{playExecution/);
 assert.match(fx,/float eliminationFxSeconds\(\) \{ return SurvivalEliminationFallMilliseconds \/ 1000\.0f; \}/);
 assert.doesNotMatch(fx,/drawDot|drawSegment/);
 assert.match(popup,/addEliminationFxButton\(\{23,18\}\);/);
 assert.match(popup,/if \(m_eliminationFx\) if \(auto popup = m_eliminationFx->lock\(\)\) popup->dismiss\(\);/);
 assert.match(popup,/corum::ranked::achievementApGrade\(view\.achievements\.totalAp\)/);
});
