import {describe,it,expect} from "vitest";
import {ACHIEVEMENT_DEFINITIONS,PROGRESSIVE_ACHIEVEMENTS,ONE_SHOT_ACHIEVEMENTS} from "../src/achievements.js";
describe("Achievement System v2 catalog",()=>{
 it("contains exactly 48 distinct achievements in the 20/19/9 scopes",()=>{
  expect(ACHIEVEMENT_DEFINITIONS).toHaveLength(48);
  expect(new Set(ACHIEVEMENT_DEFINITIONS.map(d=>d.id)).size).toBe(48);
  expect(["SOLO_RANKED","SURVIVAL","SHARED"].map(scope=>ACHIEVEMENT_DEFINITIONS.filter(d=>d.scope===scope).length)).toEqual([20,19,9]);
 });
 it("uses the same five display colors without renaming persisted grade identifiers",()=>{
  for(const d of PROGRESSIVE_ACHIEVEMENTS){
   expect(d.levels.map(l=>l.ap)).toEqual([5,10,20,40,75]);
   expect(d.levels.map(l=>l.grade)).toEqual(["BRONZE","SILVER","GOLD","PLATINUM","DIAMOND"]);
   expect(d.levels.map(l=>l.displayColor)).toEqual(["#B2BAC9","#57BBFF","#C679FF","#FFB046","#8DEBFF"]);
  }
 });
 it("gives new Mythic unlocks 200 AP",()=>{
  const definition=ONE_SHOT_ACHIEVEMENTS.find(d=>d.id==="deathmatch_double_hundred");
  expect(definition).toMatchObject({rarity:"MYTHIC",ap:200});
 });
});
it('uses every requested Survival/shared I–V threshold exactly',()=>{
 const thresholds={survival_matches:[5,25,100,250,500],survival_wins:[1,3,10,25,50],survival_podiums:[1,5,20,50,100],survival_finals:[1,5,20,50,100],survival_round_survives:[10,50,200,500,1000],survival_round_wins:[1,10,25,50,100],survival_podium_streak:[2,3,5,8,12],shared_matches:[20,100,250,500,1000],shared_wins:[5,25,100,250,500],shared_mastery:[25,100,300,700,1500],shared_clears:[25,100,300,700,1500]};
 for(const [id,expected]of Object.entries(thresholds))expect(PROGRESSIVE_ACHIEVEMENTS.find(d=>d.id===id)?.levels.map(l=>l.threshold)).toEqual(expected);
});

it('has the exact Survival one-shot rarities/AP and the requested maximum reward',()=>{
 const expected={survival_first_match:['COMMON',10],survival_cutoff_escape:['COMMON',10],survival_first_podium:['RARE',25],survival_round_perfect:['RARE',25],survival_first_win:['EPIC',50],survival_underdog:['EPIC',50],survival_deathmatch_entry:['EPIC',50],survival_secret_1:['EPIC',75],survival_clean_champion:['LEGENDARY',100],survival_final_perfect_win:['LEGENDARY',100],survival_true_champion:['MYTHIC',200],survival_secret_2:['MYTHIC',200]};
 const singles=ONE_SHOT_ACHIEVEMENTS.filter(d=>d.scope==='SURVIVAL');
 expect(Object.fromEntries(singles.map(d=>[d.id,[d.rarity,d.ap]]))).toEqual(expected);
 const progressive=PROGRESSIVE_ACHIEVEMENTS.filter(d=>d.scope==='SURVIVAL');
 expect(progressive).toHaveLength(7);
 expect(progressive.flatMap(d=>d.levels).reduce((a,l)=>a+l.ap,0)).toBe(1050);
 expect(singles.reduce((a,d)=>a+d.ap,0)).toBe(895);
 expect(singles.filter(d=>d.hidden).map(d=>d.id)).toEqual(['survival_secret_1','survival_secret_2']);
 expect(singles.find(d=>d.id==='survival_secret_1')?.hintKo).toBe('일반 매치에서 달성 가능');
 expect(singles.find(d=>d.id==='survival_secret_2')?.hintKo).toBe('데스매치에서 달성 가능');
});
