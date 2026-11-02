import {describe,it,expect} from "vitest";
import {createSurvivalMatch,reduceSurvivalMatch,survivalLiveView,survivalMapSelectionTarget,survivalIntroEndsAt} from "../src/survival-match.js";
import {DEFAULT_SURVIVAL_CONFIG} from "../src/survival.js";
import {survivalMapAllowed,survivalMapRange} from "../src/survival-maps.js";
import {survivalOpenDifficultyVote,survivalVoteWeights,survivalSliceOrder,survivalSliceRange,survivalEffectiveBallot,
  SURVIVAL_VOTE_OPENS_MS,SURVIVAL_VOTE_MS,SURVIVAL_VOTE_OUTRO_MS} from "../src/survival-vote.js";

const SEED="0123456789abcdef0123456789abcdef";
const fixture=(humans=2)=>{
  const m=createSurvivalMatch("vote",Array.from({length:16},(_,i)=>({participantId:`p${i}`,playerId:i<humans?`h${i}`:null,name:`P${i}`,
    kind:i<humans?"HUMAN" as const:"BOT" as const,lp:150})),DEFAULT_SURVIVAL_CONFIG,"v1",1000);
  m.presentationControlled=true;m.presentationGate="INTRO";m.difficultyVote=survivalOpenDifficultyVote(1000,SEED);return m;
};
const opens=1000+SURVIVAL_VOTE_OPENS_MS,closes=opens+SURVIVAL_VOTE_MS;
const vote=(participantId:string,ballot:"EASY"|"NORMAL"|"HARD"|"NONE")=>({type:"DIFFICULTY_VOTE" as const,roundKey:"stage-1",mapGeneration:1,participantId,ballot});

describe("Survival difficulty vote",()=>{
  it("weights ranks 55/35/10 and splits ties",()=>{
    expect(survivalVoteWeights({EASY:7,NORMAL:4,HARD:5})).toEqual({EASY:550,HARD:350,NORMAL:100});
    expect(survivalVoteWeights({EASY:3,NORMAL:3,HARD:1})).toEqual({EASY:450,NORMAL:450,HARD:100});
    expect(survivalVoteWeights({EASY:2,NORMAL:1,HARD:1})).toEqual({EASY:550,NORMAL:225,HARD:225});
    const all=survivalVoteWeights({EASY:1,NORMAL:1,HARD:1});
    expect(all.EASY+all.NORMAL+all.HARD).toBe(1000);expect(all.HARD).toBe(334);
  });
  it("draws the weighted slice first and falls back by weight",()=>{
    const w={EASY:550,NORMAL:100,HARD:350};
    expect(survivalSliceOrder(w,0)).toEqual(["EASY","HARD","NORMAL"]);
    expect(survivalSliceOrder(w,549)[0]).toBe("EASY");
    expect(survivalSliceOrder(w,550)).toEqual(["NORMAL","EASY","HARD"]);
    expect(survivalSliceOrder(w,999)[0]).toBe("HARD");
    expect(()=>survivalSliceOrder(w,1000)).toThrow();
  });
  it("splits allowed star ranges into easy, normal and hard slices inside the range",()=>{
    const r=survivalMapRange("SILVER","ROUND_1");
    expect(survivalSliceRange(r,"EASY")).toEqual({minimumStars:5,maximumStars:5,demons:[]});
    expect(survivalSliceRange(r,"NORMAL")).toEqual({minimumStars:6,maximumStars:6,demons:[]});
    expect(survivalSliceRange(r,"HARD")).toEqual({minimumStars:7,maximumStars:7,demons:[]});
    const final=survivalMapRange("RED_BRONZE","FINAL");
    expect(survivalSliceRange(final,"NORMAL")).toEqual({minimumStars:8,maximumStars:9,demons:[]});
    expect(survivalSliceRange(final,"HARD")).toEqual({minimumStars:null,maximumStars:null,demons:["EASY"]});
    const two=survivalMapRange("SILVER","FINAL");
    expect(survivalSliceRange(two,"NORMAL").demons).toEqual(["EASY","MEDIUM"]);
    expect(survivalSliceRange(survivalMapRange("GOLD","TIEBREAK"),"EASY").demons).toEqual(["INSANE"]);
    const map={levelId:40000001,stars:7,demon:false,demonDifficulty:null,automatic:false,platformer:false};
    expect(survivalMapAllowed(map,"SILVER","ROUND_1",40000000,"HARD")).toBe(true);
    expect(survivalMapAllowed(map,"SILVER","ROUND_1",40000000,"EASY")).toBe(false);
  });
  it("holds the first map draw and the intro until the vote resolves, then closes early when all humans voted",()=>{
    let m=fixture();
    expect(survivalMapSelectionTarget(m)).toBeNull();
    expect(()=>reduceSurvivalMatch(m,vote("p0","HARD"),opens-1)).toThrow();
    m=reduceSurvivalMatch(m,vote("p0","HARD"),opens+100);
    expect(m.difficultyVote!.resolvedAt).toBeNull();
    expect(()=>reduceSurvivalMatch(m,vote("p0","EASY"),opens+200)).toThrow();
    expect(()=>reduceSurvivalMatch(m,vote("p5","EASY"),opens+200)).toThrow();
    m=reduceSurvivalMatch(m,vote("p1","NONE"),opens+300);
    expect(m.difficultyVote!.resolvedAt).toBe(opens+300);
    const w=m.difficultyVote!.weights!;expect(w.EASY+w.NORMAL+w.HARD).toBe(1000);
    expect(survivalMapSelectionTarget(m)?.phase).toBe("ROUND_1");
    expect(survivalIntroEndsAt(m)).toBe(Math.max(1000+6000,opens+300+SURVIVAL_VOTE_OUTRO_MS));
    expect(survivalEffectiveBallot(m.difficultyVote!,"p1")).toMatch(/^(EASY|NORMAL|HARD)$/);
  });
  it("resolves at the deadline with missing ballots as random hidden votes",()=>{
    let m=fixture();
    m=reduceSurvivalMatch(m,{type:"TICK"},closes-1);expect(m.difficultyVote!.resolvedAt).toBeNull();expect(m.presentationGate).toBe("INTRO");
    m=reduceSurvivalMatch(m,{type:"TICK"},closes);expect(m.difficultyVote!.resolvedAt).toBe(closes);
    expect(m.presentationGate).toBe("INTRO");
    m=reduceSurvivalMatch(m,{type:"TICK"},closes+SURVIVAL_VOTE_OUTRO_MS);expect(m.presentationGate).toBeNull();
  });
  it("never exposes ballots, tallies or weights to clients",()=>{
    let m=fixture();
    m=reduceSurvivalMatch(m,vote("p0","EASY"),opens+10);
    const view=survivalLiveView(m,opens+10,"p1");
    expect(view.difficultyVote).toEqual({opensAt:new Date(opens).toISOString(),closesAt:new Date(closes).toISOString(),
      resolved:false,votedCount:1,totalCount:16,myBallot:null});
    expect(survivalLiveView(m,opens+10,"p0").difficultyVote?.myBallot).toBe("EASY");
    const text=JSON.stringify(survivalLiveView(m,closes+10,"p1"));
    expect(text).not.toContain(SEED);expect(text).not.toContain("weights");expect(text).not.toContain("ballots");
    expect(survivalLiveView(reduceSurvivalMatch(m,{type:"TICK"},closes),closes,"p1").difficultyVote?.votedCount).toBe(16);
  });
});
