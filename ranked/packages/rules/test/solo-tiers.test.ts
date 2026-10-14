import { describe,it,expect } from "vitest";
import { expandSoloTierBands,soloBaseTierBands,goldTierForPosition,goldTierSlots,tierForRating,tierBandFor,
  validateOperationalConfig,finalizePlacementScore,updateRankedRatingProfile,type RankedRatingProfile } from "../src/index.js";
import { completeOperationalConfig,tierBandsFixture } from "./fixtures.js";
const bands=expandSoloTierBands(tierBandsFixture);
const config=completeOperationalConfig(),policy=config.mmrPolicy!;
const profile=(tier:RankedRatingProfile["displayedTier"],score:number):RankedRatingProfile=>({
 displayedTier:tier,visibleRankedScore:score,hiddenMmr:score,placementGames:policy.placementGames,
 placementResults:[],resultStreak:0,promotion:null,demotionWarning:false });
describe("Solo divisions",()=>{
 it("preserves operator pools, base boundaries and series while expanding legacy rows",()=>{
  expect(bands).toHaveLength(13);expect(soloBaseTierBands(bands)).toEqual(tierBandsFixture);
  expect(expandSoloTierBands(bands)).toBe(bands);
  expect(bands.slice(0,3).map(b=>[b.tier,b.minInclusive,b.maxExclusive,b.promotionGames])).toEqual([
   ["RED3",0,334,0],["RED2",334,667,0],["RED1",667,1000,tierBandsFixture[0]!.promotionGames]]);
  expect(validateOperationalConfig({...config,tierBands:bands}).errors).toEqual([]);
 });
 it("uses each configured division pool at its exact LP boundary",()=>{
  const custom=bands.map(b=>b.tier==="RED2"?{...b,mainPool:4 as const,deathmatchPool:5 as const}:b);
  expect(tierForRating(333,custom)).toBe("RED3");expect(tierForRating(334,custom)).toBe("RED2");
  expect(tierBandFor("RED2",custom)).toMatchObject({mainPool:4,deathmatchPool:5});
 });
 it("moves inside a tier immediately with no series or protection",()=>{
  const up=updateRankedRatingProfile(profile("RED3",330),"A","A",330,policy,bands);
  expect(up.profile.displayedTier).toBe("RED2");expect(up.profile.promotion).toBeNull();expect(up.promotionStarted).toBe(false);
  const down=updateRankedRatingProfile(profile("RED2",340),"A","B",340,policy,bands);
  expect(down.profile.displayedTier).toBe("RED3");expect(down.profile.visibleRankedScore).toBeLessThan(334);
  expect(down.profile.demotionWarning).toBe(false);expect(down.demoted).toBe(true);
 });
 it("retains series at the next base tier and protection at division III only",()=>{
  const up=updateRankedRatingProfile(profile("RED1",990),"A","A",990,policy,bands);
  expect(up.profile.promotion).toMatchObject({sourceTier:"RED1",targetTier:"AQUA3",cutoff:1000});
  const down=updateRankedRatingProfile(profile("AQUA3",1001),"A","B",500,policy,bands);
  expect(down.profile.displayedTier).toBe("AQUA3");expect(down.profile.demotionWarning).toBe(true);
  const demote=updateRankedRatingProfile(down.profile,"A","B",500,policy,bands);
  expect(demote.profile.displayedTier).toBe("RED1");expect(demote.demoted).toBe(true);
 });
 it("does not apply placement exclusion at numbered boundaries",()=>{
  expect(finalizePlacementScore(334,bands)).toEqual({visibleRankedScore:334,displayedTier:"RED2"});
  expect(finalizePlacementScore(1000,bands)).toEqual({visibleRankedScore:984,displayedTier:"RED1"});
 });
 it.each([1,2,3,4,5,6,7,8,9,10,101])("allocates exact gold slots for %i players",total=>{
  const titles=Array.from({length:total},(_,i)=>goldTierForPosition(i+1,total));
  const champion=Math.min(3,total),master=total>6?Math.ceil(total/2)-3:0;
  expect(titles.filter(t=>t==="CHAMPION")).toHaveLength(champion);
  expect(titles.filter(t=>t==="MASTER")).toHaveLength(master);
  expect(goldTierSlots(total).gold).toBe(total-champion-master);
 });
});
