import { describe, expect, it } from "vitest";
import { changeMatchPause, emptyMatchPause, matchPauseView } from "../src/match/match-pause.policy.js";
describe("match pause lease ordering", () => {
  it("stale Starts/Ends cannot reopen or replace another lease; expiry uses wall time", () => {
    let state=changeMatchPause(emptyMatchPause(),"one",true,0);
    state=changeMatchPause(state,"one",false,10000);
    expect(matchPauseView(changeMatchPause(state,"one",true,12000),12000).active).toBe(false);
    state=changeMatchPause(state,"two",true,12000);
    state=changeMatchPause(state,"one",false,13000);
    expect(state.leaseId).toBe("two");
    state=changeMatchPause(state,"two",false,15000);
    expect(matchPauseView(changeMatchPause(state,"one",true,16000),16000).active).toBe(false);
    state=changeMatchPause(state,"three",true,17000);
    expect(matchPauseView(state,34000)).toMatchObject({active:false,remainingMs:0});
    state=changeMatchPause(state,"three",false,40000);
    expect(state.usedMs).toBe(30000);
  });
});
