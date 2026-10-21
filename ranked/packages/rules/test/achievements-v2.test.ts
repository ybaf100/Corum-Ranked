import {describe,it,expect} from "vitest";
import {ACHIEVEMENT_DEFINITIONS,PROGRESSIVE_ACHIEVEMENTS,ONE_SHOT_ACHIEVEMENTS} from "../src/achievements.js";
describe("Achievement System v2 catalog",()=>{
 it("contains exactly 46 distinct achievements in the 20/18/8 scopes",()=>{
  expect(ACHIEVEMENT_DEFINITIONS).toHaveLength(46);
  expect(new Set(ACHIEVEMENT_DEFINITIONS.map(d=>d.id)).size).toBe(46);
  expect(["SOLO_RANKED","SURVIVAL","SHARED"].map(scope=>ACHIEVEMENT_DEFINITIONS.filter(d=>d.scope===scope).length)).toEqual([20,18,8]);
 });
 it("uses the same five display colors without renaming persisted grade identifiers",()=>{
  for(const d of PROGRESSIVE_ACHIEVEMENTS){
   expect(d.levels.map(l=>l.ap)).toEqual([5,10,20,40,75]);
   expect(d.levels.map(l=>l.grade)).toEqual(["BRONZE","SILVER","GOLD","PLATINUM","DIAMOND"]);
   expect(d.levels.map(l=>l.displayColor)).toEqual(["#B2BAC9","#57BBFF","#C679FF","#FFB046","#E5E4E2"]);
  }
 });
 it("gives new Mythic unlocks 200 AP",()=>{
  const definition=ONE_SHOT_ACHIEVEMENTS.find(d=>d.id==="deathmatch_double_hundred");
  expect(definition).toMatchObject({rarity:"MYTHIC",ap:200});
 });
});
it('uses every requested Survival/shared I–V threshold exactly',()=>{
 const thresholds={survival_matches:[5,25,100,250,500],survival_wins:[1,3,10,25,50],survival_podiums:[1,5,20,50,100],survival_finals:[1,5,20,50,100],survival_round_survives:[10,50,200,500,1000],survival_round_wins:[1,10,25,50,100],survival_podium_streak:[2,3,5,8,12],survival_win_streak:[2,3,5,7,10],shared_matches:[20,100,250,500,1000],shared_wins:[5,25,100,250,500],shared_mastery:[25,100,300,700,1500]};
 for(const [id,expected]of Object.entries(thresholds))expect(PROGRESSIVE_ACHIEVEMENTS.find(d=>d.id===id)?.levels.map(l=>l.threshold)).toEqual(expected);
});
