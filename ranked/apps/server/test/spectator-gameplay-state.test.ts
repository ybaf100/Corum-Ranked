import { describe, expect, it } from "vitest";
import { parseSpectatorKeyframe, SPECTATOR_GAME_MODES } from "../src/match/spectator-live.service.js";

const pose = () => ({ x: 100, y: 90, rotation: 270, mode: "CUBE", mini: false,
  upsideDown: false, flipped: false, dual: false, dead: false, finished: false });
describe("strict spectator gameplay protocol v2", () => {
  for (const mode of SPECTATOR_GAME_MODES) {
    it(`roundtrips ${mode} with native size/gravity/visual flip`, () => {
      for (const value of [false, true, false]) {
        const keyframe = { ...pose(), mode, mini: value, upsideDown: value, flipped: value };
        expect(parseSpectatorKeyframe(keyframe)).toEqual(keyframe);
      }
    });
  }
  it("keeps optional native source camera in v2 and ignores malformed optional camera without weakening snap endpoints", () => {
    const discontinuity={type:"SPIDER_SNAP",fromX:100,fromY:80,toX:100,toY:740,cameraFromX:20,cameraFromY:40};
    expect(parseSpectatorKeyframe({...pose(),mode:"SPIDER",y:740,discontinuity}).discontinuity).toEqual(discontinuity);
    const malformed={...discontinuity,cameraFromX:1e20};
    expect(parseSpectatorKeyframe({...pose(),discontinuity:malformed}).discontinuity!.cameraFromX).toBeUndefined();
    expect(()=>parseSpectatorKeyframe({...pose(),discontinuity:{...discontinuity,toY:100000}})).toThrow();
  });
  it("rejects unknown/missing modes and incomplete v1 keyframes", () => {
    for (const mode of [undefined, null, "cube", "JETPACK", "UNKNOWN", 1]) {
      expect(() => parseSpectatorKeyframe({ ...pose(), mode })).toThrow();
    }
    expect(() => parseSpectatorKeyframe({ x: 1, y: 2, rotation: 3, dead: false, finished: false })).toThrow();
  });
  it("requires actual booleans for every discrete state", () => {
    for (const key of ["mini", "upsideDown", "flipped", "dual", "dead", "finished"]) {
      for (const value of [undefined, 0, "false", null]) {
        expect(() => parseSpectatorKeyframe({ ...pose(), [key]: value })).toThrow();
      }
    }
  });
  it("rejects non-finite and out of range poses", () => {
    for (const key of ["x", "y", "rotation"]) {
      for (const value of [NaN, Infinity, -Infinity, "100", 100_000_001]) {
        expect(() => parseSpectatorKeyframe({ ...pose(), [key]: value })).toThrow();
      }
    }
  });
  it("requires mutually exclusive terminal states without turning them into score authority", () => {
    expect(() => parseSpectatorKeyframe({ ...pose(), dead: true, finished: true })).toThrow();
    expect(parseSpectatorKeyframe({ ...pose(), dead: true }).dead).toBe(true);
    expect(parseSpectatorKeyframe({ ...pose(), finished: true }).finished).toBe(true);
  });
});
