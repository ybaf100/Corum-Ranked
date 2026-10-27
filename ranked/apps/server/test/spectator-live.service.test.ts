import { BadRequestException, ForbiddenException } from "@nestjs/common";
import { afterEach, describe, expect, it, vi } from "vitest";
import { InMemoryMatchRuntimeState } from "../src/match/match-runtime-state.js";
import { MatchService } from "../src/match/match.service.js";
import { SpectatorLiveService, parseSpectatorKeyframe } from "../src/match/spectator-live.service.js";
import type { RankedSessionContext } from "../src/session/session.types.js";

const session = { playerId: "player-a" } as RankedSessionContext;

const standardState = (
  side: "A" | "B",
  spectator: boolean,
  state = "ROUND_SETTLING",
  roundNumber = 2,
) => ({
  matchId: "match-1",
  state,
  side,
  opponentPractice: false,
  opponentPaused: false,
  pause: { active: false },
  players: {
    A: { gdUsername: "alpha" },
    B: { gdUsername: "bravo" },
  },
  currentRound: { roundNumber, map:{playableLevelId:"123"} },
  deathmatch: null,
  spectator: spectator
    ? { active: true, opponentName: side === "A" ? "bravo" : "alpha", currentProgress: 42 }
    : { active: false },
});

const packet = (overrides: Record<string, unknown> = {}) => ({
  protocolVersion: 2,
  matchId: "match-1",
  roundNumber: 2,
  deathmatchSequence: null,
  attemptId: "attempt-b",
  generation: 1,
  sequence: 0,
  elapsedMs: 10,
  inputEdges: [],
  keyframe: {
    x: 120,
    y: 48,
    rotation: 15,
    mode: "CUBE", mini: false, upsideDown: false, flipped: false, dual: false,
    dead: false,
    finished: false,
  },
  ...overrides,
});

const setup = () => {
  let state = standardState("B", false);
  const stateCall = vi.fn(async () => state);
  const matches = { state: stateCall } as unknown as MatchService;
  const runtime = new InMemoryMatchRuntimeState();
  const service = new SpectatorLiveService(matches, runtime);
  return {
    service,
    runtime,
    stateCall,
    setState(next: ReturnType<typeof standardState>) {
      state = next;
    },
  };
};

describe("SpectatorLiveService", () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it("holds an authorized opponent pause without timeout, then resumes the same generation and gameplay clock", async () => {
    const { service, runtime, setState } = setup();
    let now = 10_000; vi.spyOn(Date, "now").mockImplementation(() => now);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", now);
    await service.publish("match-1", "token", session, packet());
    service.notePublisherPause("match-1", "B", true);
    setState({ ...standardState("A", true), opponentPaused: true });
    now += 12_000;
    const held = await service.subscribe("match-1", "token", session, "2");
    expect(held).toMatchObject({ paused: true, fallback: false, waiting: false });
    expect(held.stream!.generation).toBe(1);
    service.notePublisherPause("match-1", "B", false);
    setState(standardState("B", false));
    await service.publish("match-1", "token", session, packet({ sequence: 1, elapsedMs: 60 }));
    setState(standardState("A", true));
    const resumed = await service.subscribe("match-1", "token", session, "2", "attempt-b", "1", "0");
    expect(resumed).toMatchObject({ paused: false, reset: false, fallback: false });
    expect(resumed.packets[0]).toMatchObject({ sequence: 1, generation: 1, elapsedMs: 60 });
    now += 1501;
    expect((await service.subscribe("match-1", "token", session, "2")).reason).toBe("STREAM_TIMEOUT");
  });

  it("accepts ordered 50ms/native-snap batches, retries immutable ACKs and rejects whole malformed batches atomically", async () => {
    const { service, runtime, setState, stateCall } = setup(); runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
    const samples = [0,50,60,100,110].map((elapsedMs,sequence) => packet({elapsedMs,sequence}));
    samples[2] = packet({sequence:2,elapsedMs:60,keyframe:{ ...samples[2]!.keyframe, mode:"SPIDER",upsideDown:true,
      discontinuity:{type:"SPIDER_SNAP",fromX:120,fromY:48,toX:120,toY:740,cameraFromX:100,cameraFromY:40},y:740 }});
    expect(await service.publish("match-1","token",session,{packets:samples})).toMatchObject({accepted:true,sequence:4});
    expect(stateCall).toHaveBeenCalledTimes(1); // authenticate once per ordered batch
    expect(await service.publish("match-1","token",session,{packets:samples})).toMatchObject({sequence:4});
    await expect(service.publish("match-1","token",session,{packets:[packet({sequence:5,elapsedMs:250}),packet({sequence:6,elapsedMs:200})]})).rejects.toThrow("SPECTATOR_BATCH_ORDER");
    const changed=structuredClone(samples); changed[1]!.keyframe.x=999;
    await expect(service.publish("match-1","token",session,{packets:changed})).rejects.toThrow("SPECTATOR_STALE_SEQUENCE");
    setState(standardState("A",true)); const stream=await service.subscribe("match-1","token",session);
    expect(stream.packets.map(p=>p.sequence)).toEqual([0,1,2,3,4]);
    expect(stream.packets[2]!.keyframe.discontinuity).toMatchObject({cameraFromX:100,cameraFromY:40});
  });
  it("drains queued presentation after exact End ACK without changing Ranked authority", async () => {
    const { service,runtime,setState }=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
    runtime.endAttempt("match-1",2,"B","attempt-b",false);
    await service.publish("match-1","token",session,{packets:[packet(),packet({sequence:1,elapsedMs:50})]});
    const terminal=packet({sequence:2,elapsedMs:60,keyframe:{...packet().keyframe,dead:true}});
    await service.publish("match-1","token",session,terminal);
    await service.publish("match-1","token",session,terminal); // immutable terminal ACK retry
    setState(standardState("A",true));expect((await service.subscribe("match-1","token",session)).packets.at(-1)!.keyframe.dead).toBe(true);
    expect(runtime.progress("match-1",2,"B")).toBeNull();
    expect(runtime.endedAttempt("match-1",2,"B")!.cleared).toBe(false);
  });
  it("late-join and expired-cursor replies always fit the client presentation window", async () => {
    const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
    for(let sequence=0;sequence<100;++sequence)await service.publish("match-1","token",session,packet({sequence,elapsedMs:sequence*50}));
    setState(standardState("A",true));
    const late=await service.subscribe("match-1","token",session);
    expect(late.reset).toBe(true);expect(late.packets.length).toBeLessThanOrEqual(16);
    expect(late.packets.at(-1)!.elapsedMs-late.packets[0]!.elapsedMs).toBeLessThanOrEqual(350);
    const recovered=await service.subscribe("match-1","token",session,"2","attempt-b","1","0");
    expect(recovered).toMatchObject({reset:true,fallback:false});
    expect((await service.subscribe("match-1","token",session,"2","attempt-b","1","99")).stream!.lastSequence).toBe(99);
  });
  it("rejects stream subscription for a viewer without spectator.active", async () => {
    const { service, stateCall } = setup();
    await expect(
      service.subscribe("match-1", "match-token", session),
    ).rejects.toBeInstanceOf(ForbiddenException);
    expect(stateCall).toHaveBeenCalledWith("match-1", "match-token", session);
  });

  it("propagates MatchService authentication failures for invalid match tokens", async () => {
    const { service, stateCall } = setup();
    stateCall.mockRejectedValueOnce(new ForbiddenException("invalid match token"));
    await expect(
      service.subscribe("match-1", "wrong-token", session),
    ).rejects.toThrow("invalid match token");
  });

  it("authenticates publish against the active authoritative Attempt", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await expect(
      service.publish("match-1", "match-token", session, packet()),
    ).resolves.toMatchObject({ accepted: true, attemptId: "attempt-b", sequence: 0 });
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ attemptId: "old-attempt", generation: 2 }),
      ),
    ).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });

  it("rejects route/body match and authoritative round mismatches", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await expect(
      service.publish("match-1", "match-token", session, packet({ matchId: "match-2" })),
    ).rejects.toBeInstanceOf(BadRequestException);
    await expect(
      service.publish("match-1", "match-token", session, packet({ roundNumber: 3 })),
    ).rejects.toThrow("SPECTATOR_CONTEXT_MISMATCH");
  });

  it("suppresses Practice gameplay before any packet reaches the relay", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    runtime.setPractice("match-1", 2, "B", true);
    await expect(
      service.publish("match-1", "match-token", session, packet()),
    ).rejects.toThrow("PRACTICE_STREAM_SUPPRESSED");
  });

  it("drops stale generation and reverse sequence packets", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet({ generation: 2 }));
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ generation: 1, sequence: 1 }),
      ),
    ).rejects.toThrow("SPECTATOR_STALE_GENERATION");
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ generation: 2, sequence: 0 }),
      ),
    ).rejects.toThrow("SPECTATOR_STALE_SEQUENCE");
  });

  it("provides a bounded late-join buffer and then an incremental live handoff", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    await service.publish(
      "match-1",
      "match-token",
      session,
      packet({ sequence: 1, elapsedMs: 60, keyframe: { ...packet().keyframe, x: 126, y: 50, rotation: 18, dead: false, finished: false } }),
    );

    setState(standardState("A", true));
    const lateJoin = await service.subscribe("match-1", "match-token", session, "2");
    expect(lateJoin).toMatchObject({ active: true, waiting: false, fallback: false, reset: true });
    expect(lateJoin.packets.map((item) => item.sequence)).toEqual([0, 1]);
    expect(lateJoin.packets[0]).not.toHaveProperty("receivedAtMs");

    setState(standardState("B", false));
    await service.publish(
      "match-1",
      "match-token",
      session,
      packet({ sequence: 2, elapsedMs: 110, keyframe: { ...packet().keyframe, x: 132, y: 54, rotation: 20, dead: false, finished: false } }),
    );
    setState(standardState("A", true));
    const incremental = await service.subscribe(
      "match-1",
      "match-token",
      session,
      "2",
      "attempt-b",
      "1",
      "1",
    );
    expect(incremental).toMatchObject({ fallback: false, reset: false });
    expect(incremental.packets.map((item) => item.sequence)).toEqual([2]);
  });

  it("bounds late join to the live 350ms / 16-sample edge while retaining the three-second relay", async () => {
    const { service, runtime, setState } = setup();
    const now = vi.spyOn(Date, "now").mockReturnValue(10_000);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    for (let sequence = 0; sequence < 80; sequence += 1) {
      await service.publish("match-1", "match-token", session, packet({ sequence }));
    }
    setState(standardState("A", true));
    const capped = await service.subscribe("match-1", "match-token", session, "2");
    expect(capped.packets).toHaveLength(16);
    expect(capped.packets[0]?.sequence).toBe(64);
    expect(capped.packets.at(-1)?.sequence).toBe(79);

    setState(standardState("B", false));
    service.clearAttempt("match-1", "attempt-b");
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    runtime.beginAttempt("match-1", 2, "B", "attempt-new", 11_000);
    now.mockReturnValue(20_000);
    await service.publish("match-1", "match-token", session, packet({ attemptId: "attempt-new" }));
    for (const [sequence, timestamp] of [[1, 21_000], [2, 22_000], [3, 23_001]] as const) {
      now.mockReturnValue(timestamp);
      await service.publish("match-1", "match-token", session, packet({
        attemptId: "attempt-new",
        sequence,
      }));
    }
    setState(standardState("A", true));
    const windowed = await service.subscribe("match-1", "match-token", session, "2");
    expect(windowed.packets.map((item) => item.sequence)).toEqual([1, 2, 3]);
  });

  it("returns polling fallback for unsupported protocol versions", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    setState(standardState("A", true));
    const result = await service.subscribe("match-1", "match-token", session, "99");
    expect(result).toMatchObject({ fallback: true, reason: "UNSUPPORTED_PROTOCOL_VERSION" });
  });

  it("rejects a large publisher sequence gap instead of fabricating missing gameplay", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    await expect(
      service.publish("match-1", "match-token", session, packet({ sequence: 20 })),
    ).rejects.toThrow("SPECTATOR_SEQUENCE_GAP");
  });

  it("resets an expired subscriber cursor without destroying the publisher stream", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    for (let sequence = 0; sequence < 80; sequence += 1) {
      await service.publish("match-1", "match-token", session, packet({ sequence }));
    }
    setState(standardState("A", true));
    const result = await service.subscribe(
      "match-1",
      "match-token",
      session,
      "2",
      "attempt-b",
      "1",
      "0",
    );
    expect(result).toMatchObject({ fallback: false, reset: true });
  });

  it("discards a timed-out stream and requires a fresh sequence-zero generation", async () => {
    const { service, runtime, setState } = setup();
    const now = vi.spyOn(Date, "now").mockReturnValue(10_000);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    now.mockReturnValue(11_501);
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ fallback: true, reason: "STREAM_TIMEOUT" });

    setState(standardState("B", false));
    await expect(
      service.publish("match-1", "match-token", session, packet({ sequence: 1 })),
    ).rejects.toThrow("SPECTATOR_STREAM_MUST_START_AT_SEQUENCE_ZERO");
    await expect(
      service.publish("match-1", "match-token", session, packet({ generation: 2 })),
    ).resolves.toMatchObject({ generation: 2, sequence: 0 });
  });

  it("does not invoke scoring/result APIs while relaying telemetry", async () => {
    const { service, runtime, stateCall } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    expect(stateCall).toHaveBeenCalledTimes(1);
    expect(runtime.progress("match-1", 2, "B")?.attemptId).toBe("attempt-b");
  });

  it("will not apply packets from an old Attempt after reconnect", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    runtime.beginAttempt("match-1", 2, "B", "attempt-new", 2_000);
    await expect(
      service.publish(
        "match-1",
        "match-token",
        session,
        packet({ attemptId: "attempt-b", sequence: 1 }),
      ),
    ).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });

  it("removes buffers on Attempt end, round change, and match end", async () => {
    const { service, runtime, setState } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", 1_000);
    await service.publish("match-1", "match-token", session, packet());
    service.clearAttempt("match-1", "attempt-b");
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });

    setState(standardState("B", false));
    await service.publish("match-1", "match-token", session, packet());
    runtime.beginAttempt("match-1", 3, "B", "attempt-new", 2_000);
    setState(standardState("B", false, "ROUND_PLAYING", 3));
    await service.publish("match-1", "match-token", session, packet({
      roundNumber: 3,
      attemptId: "attempt-new",
    }));
    setState(standardState("A", true));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });

    setState(standardState("B", false, "ROUND_PLAYING", 3));
    await service.publish("match-1", "match-token", session, packet({
      roundNumber: 3,
      attemptId: "attempt-new",
    }));
    service.decorateState({
      ...standardState("A", false, "MATCH_RESULT", 3),
      currentRound: null,
    });
    setState(standardState("A", true, "ROUND_PLAYING", 3));
    await expect(
      service.subscribe("match-1", "match-token", session, "2"),
    ).resolves.toMatchObject({ waiting: true, reason: "STREAM_NOT_READY" });
  });
  it("keeps the exact server-ended Attempt for terminal HTTP races, then expires it", async () => {
    const { service, runtime, setState } = setup();
    let now = 10000; vi.spyOn(Date, "now").mockImplementation(() => now);
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", now);
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", false);
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 1, elapsedMs: 60, keyframe: { ...packet().keyframe, dead: true } }))).resolves.toMatchObject({ accepted: true });
    setState(standardState("A", true));
    expect((await service.subscribe("match-1", "match-token", session, "2")).packets.at(-1)?.keyframe.dead).toBe(true);
    now += 1501;
    expect((await service.subscribe("match-1", "match-token", session, "2")).waiting).toBe(true);
    setState(standardState("B", false));
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 2, keyframe: { ...packet().keyframe, dead: true } }))).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
  });
  it("rejects wrong ended identity/terminal outcome and reviving the same Attempt, but permits a new Attempt", async () => {
    const { service, runtime } = setup();
    runtime.beginAttempt("match-1", 2, "B", "attempt-b", Date.now());
    await service.publish("match-1", "match-token", session, packet());
    runtime.endAttempt("match-1", 2, "B", "attempt-b", true);
    await expect(service.publish("match-1", "match-token", session,
      packet({ sequence: 1, keyframe: { ...packet().keyframe, dead: true } }))).rejects.toThrow("SPECTATOR_ATTEMPT_MISMATCH");
    await service.publish("match-1", "match-token", session,
      packet({ sequence: 1, keyframe: { ...packet().keyframe, finished: true } }));
    await expect(service.publish("match-1", "match-token", session, packet({ sequence: 2 }))).rejects.toThrow();
    await expect(service.publish("match-1", "match-token", session,
      packet({ generation: 2, keyframe: { ...packet().keyframe, finished: true } }))).rejects.toThrow("SPECTATOR_TERMINAL_STREAM");
    runtime.beginAttempt("match-1", 2, "B", "new-attempt", Date.now());
    await expect(service.publish("match-1", "match-token", session,
      packet({ attemptId: "new-attempt", generation: 2 }))).resolves.toMatchObject({ accepted: true });
  });

});


describe("Stage 2 bounded native world history",()=>{
 afterEach(()=>vi.restoreAllMocks());
 for(const joinMs of [0,10000,60000,120000])it(`paginates complete ${joinMs}ms history from zero without duplicating live packets`,async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  const count=Math.floor(joinMs/50)+1;
  for(let start=0;start<count;start+=16)await service.publish("match-1","token",session,{packets:Array.from({length:Math.min(16,count-start)},(_,i)=>packet({sequence:start+i,elapsedMs:(start+i)*50,levelId:123}))});
  setState(standardState("A",true));let cursor:string|undefined;const seen:number[]=[];
  for(let page=0;page<70;page++){
   const response=await service.subscribe("match-1","token",session,"2",cursor===undefined?undefined:"attempt-b",cursor===undefined?undefined:"1",cursor,"1");
   expect(response.restoration).toMatchObject({available:true,levelId:123,startSequence:0,headSequence:count-1});
   expect(response.packets.length).toBeLessThanOrEqual(64);seen.push(...response.packets.map(p=>p.sequence));
   cursor=String(seen.at(-1));if(response.restoration!.caughtUp)break;
  }
  expect(seen).toEqual(Array.from({length:count},(_,i)=>i));
  const legacy=await service.subscribe("match-1","token",session,"2");expect(legacy.packets.length).toBeLessThanOrEqual(16);expect(legacy.restoration).toBeUndefined();
  const duplicate=await service.subscribe("match-1","token",session,"2","attempt-b","1",cursor,"1");expect(duplicate.packets).toHaveLength(0);expect(duplicate.restoration!.caughtUp).toBe(true);
 });
 it("never lets a subscriber's future cursor erase the publisher",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  await service.publish("match-1","token",session,packet({levelId:123}));setState(standardState("A",true));
  expect((await service.subscribe("match-1","token",session,"2","attempt-b","1","100","1")).fallback).toBe(true);
  expect((await service.subscribe("match-1","token",session,"2",undefined,undefined,undefined,"1")).packets[0]!.sequence).toBe(0);
 });
 it("reports lost process history and resumes only a newly seeded publisher generation",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  await expect(service.publish("match-1","token",session,packet({sequence:100,elapsedMs:60000,levelId:123}))).rejects.toThrow("START_AT_SEQUENCE_ZERO");
  await service.publish("match-1","token",session,packet({sequence:0,generation:2,elapsedMs:60050,levelId:123}));setState(standardState("A",true));
  const response=await service.subscribe("match-1","token",session,"2",undefined,undefined,undefined,"1");
  expect(response.restoration).toMatchObject({available:false,reason:"ATTEMPT_START_MISSING"});expect(response.packets[0]!.elapsedMs).toBe(60050);
 });
 it("expires only bounded history, preserving the live stream",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  for(let start=0;start<4100;start+=16)await service.publish("match-1","token",session,{packets:Array.from({length:Math.min(16,4100-start)},(_,i)=>packet({sequence:start+i,elapsedMs:(start+i)*50,levelId:123}))});
  setState(standardState("A",true));const response=await service.subscribe("match-1","token",session,"2",undefined,undefined,undefined,"1");
  expect(response.restoration).toMatchObject({available:false,reason:"HISTORY_WINDOW_EXPIRED",startSequence:4});expect(response.packets.at(-1)!.sequence).toBe(4099);
 });
 it("bounds independent stream allocation without background polling",()=>{
  const {service}=setup();for(let i=0;i<16;++i)service.publishRecorded({...packet({levelId:123}),matchId:"debug-spectate:"+i} as never,1000);
  expect(()=>service.publishRecorded({...packet(),matchId:"debug-spectate:overflow"} as never,1000)).toThrow("SPECTATOR_STREAM_LIMIT");
  expect(service.subscribeRecorded("debug-spectate:0",1000).packets).toHaveLength(1);
 });
 it("validates all camera compositor fields atomically and preserves legacy v2",()=>{
  const legacy=packet().keyframe;expect(parseSpectatorKeyframe(legacy).cameraZoom).toBeUndefined();
  const layers=Array.from({length:3},()=>({present:true,scaleX:1.25,scaleY:1.25,rotation:42}));
  const extended={...legacy,cameraX:20,cameraY:30,cameraZoom:1.25,cameraAngle:42,cameraLayers:layers};
  expect(parseSpectatorKeyframe(extended)).toMatchObject({cameraZoom:1.25,cameraAngle:42,cameraLayers:layers});
  for(const corrupt of [{cameraZoom:0},{cameraAngle:Infinity},{cameraLayers:layers.slice(1)},{cameraLayers:[{...layers[0],scaleX:NaN},...layers.slice(1)]},{cameraLayers:[{...layers[0],present:false},...layers.slice(1)]}])expect(()=>parseSpectatorKeyframe({...extended,...corrupt})).toThrow();
 });
});


describe("Stage 2 history boundary integrity",()=>{
 afterEach(()=>vi.restoreAllMocks());
 it("rejects an exact-level mismatch without mutating the existing publisher",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  await service.publish("match-1","token",session,packet({levelId:123}));
  await expect(service.publish("match-1","token",session,packet({sequence:1,elapsedMs:50,levelId:456}))).rejects.toThrow("LEVEL_MISMATCH");
  setState(standardState("A",true));expect((await service.subscribe("match-1","token",session,"2",undefined,undefined,undefined,"1")).packets).toHaveLength(1);
 });
 it("does not advertise a capture-time hole as a complete world",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  await service.publish("match-1","token",session,{packets:[packet({levelId:123,elapsedMs:0}),packet({levelId:123,sequence:1,elapsedMs:1000})]});
  setState(standardState("A",true));expect((await service.subscribe("match-1","token",session,"2",undefined,undefined,undefined,"1")).restoration).toMatchObject({available:false,reason:"HISTORY_GAP"});
 });
 it("replaces prior Attempt history without transferring its generation or camera",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt("match-1",2,"B","attempt-b",1000);
  await service.publish("match-1","token",session,packet({levelId:123}));
  runtime.beginAttempt("match-1",2,"B","next-attempt",2000);
  await service.publish("match-1","token",session,packet({levelId:123,attemptId:"next-attempt",generation:1}));setState(standardState("A",true));
  const next=await service.subscribe("match-1","token",session,"2","attempt-b","1","100","1");
  expect(next.reset).toBe(true);expect(next.stream!.attemptId).toBe("next-attempt");expect(next.packets.every(p=>p.attemptId==="next-attempt")).toBe(true);
 });
});

describe("beta108 recorded audio and terminal restore boundary",()=>{
 afterEach(()=>vi.restoreAllMocks());
 it("keeps actual native song time distinct from percent and elapsed, rejecting malformed audio",()=>{
  const base=packet().keyframe;
  expect(parseSpectatorKeyframe({...base,audio:{trackHash:'a'.repeat(64),positionMs:62150}}).audio).toEqual({trackHash:'a'.repeat(64),positionMs:62150});
  for(const audio of [null,{}, {trackHash:'../secret',positionMs:0},{trackHash:'a'.repeat(64),positionMs:NaN},{trackHash:'a'.repeat(64),positionMs:-1},{trackHash:'a'.repeat(64),positionMs:86400001}])
   expect(()=>parseSpectatorKeyframe({...base,audio})).toThrow(BadRequestException);
  expect(parseSpectatorKeyframe(base).audio).toBeUndefined();
 });
 it("abandons obsolete catch-up after death, holds terminal and uses a clean next Attempt generation",async()=>{
  const {service,runtime,setState}=setup();runtime.beginAttempt('match-1',2,'B','attempt-b',1000);
  for(let start=0;start<200;start+=16)await service.publish('match-1','token',session,{packets:Array.from({length:Math.min(16,200-start)},(_,i)=>packet({sequence:start+i,elapsedMs:(start+i)*50,levelId:123}))});
  setState(standardState('A',true));const first=await service.subscribe('match-1','token',session,'2',undefined,undefined,undefined,'1');expect(first.restoration?.available).toBe(true);
  setState(standardState('B',false));await service.publish('match-1','token',session,packet({sequence:200,elapsedMs:10000,levelId:123,keyframe:{...packet().keyframe,dead:true}}));
  setState(standardState('A',true));const dead=await service.subscribe('match-1','token',session,'2','attempt-b','1','63','1');
  expect(dead.reset).toBe(true);expect(dead.restoration).toMatchObject({available:false,reason:'ATTEMPT_ENDED_DURING_RESTORE'});expect(dead.packets.at(-1)?.keyframe.dead).toBe(true);expect(dead.packets[0]!.sequence).toBeGreaterThan(63);
  runtime.beginAttempt('match-1',2,'B','new-attempt',20000);setState(standardState('B',false));await service.publish('match-1','token',session,packet({attemptId:'new-attempt',generation:2,levelId:123}));
  setState(standardState('A',true));const next=await service.subscribe('match-1','token',session,'2','attempt-b','1','200','1');expect(next.reset).toBe(true);expect(next.stream?.attemptId).toBe('new-attempt');expect(next.packets).toHaveLength(1);
 });
});
