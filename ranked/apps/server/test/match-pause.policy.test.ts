import { describe, expect, it } from "vitest";
import { changeMatchPause, emptyMatchPause, matchPauseView, expiredMatchPauseSide } from "../src/match/match-pause.policy.js";
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
  it("expires only an open lease at the exact accumulated deadline, independent of polling cadence", () => {
    let a = changeMatchPause(emptyMatchPause(), "first", true, 1000);
    expect(expiredMatchPauseSide({ A: a }, 30999)).toBeNull();
    expect(expiredMatchPauseSide({ A: a }, 31000)).toBe("A");
    a = changeMatchPause(a, "first", false, 13000);
    expect(expiredMatchPauseSide({ A: a }, 999999)).toBeNull();
    a = changeMatchPause(a, "second", true, 50000);
    a = changeMatchPause(a, "first", false, 60000); // stale End cannot hide expiry
    expect(expiredMatchPauseSide({ A: a }, 67999)).toBeNull();
    expect(expiredMatchPauseSide({ A: a }, 68000)).toBe("A");
    const b = changeMatchPause(emptyMatchPause(), "b", true, 36000);
    expect(expiredMatchPauseSide({ A: a, B: b }, 70000)).toBe("B"); // earliest exhausted player
    const closed = { usedMs: 30000, leaseId: "spent", startedAtMs: null };
    expect(expiredMatchPauseSide({ A: closed }, 70000)).toBeNull();
    const reopened = changeMatchPause(closed, "new", true, 70000);
    expect(expiredMatchPauseSide({ A: reopened }, 70000)).toBe("A");
  });
});
