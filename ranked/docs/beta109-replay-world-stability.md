# beta.109 replay and native world stabilization

Baseline: freshly fetched `40495dc9d042dfd059f49df2eab17fbda4ca56ee` (beta.108). This patch preserves official scoring, the cleared-player spectator policy, local Free Play, Stage 1–3 Solo presentation, the 16/64-player rules and production data. No DB migration, reset, report feature or replay upload is added.

## Evidence and diagnosis

Evidence: the supplied iPadOS `Geode 2026-10-28 00.46.06.log` and `Geode 2026-10-28 00.51.59.log`. Raw device logs are not committed.

| Item | Evidence | Conclusion |
| --- | --- | --- |
| Immediate replay failure | The first log contains native capture timing after GO, but no first rejected field, storage exception or best-file promotion. | Timing proves the official capture boundary ran; it does not prove an active recorder or a saved replay. The particular iPad failure remains unconfirmed. |
| Save-root path | Production storage rejected every ancestor symlink, including an OS-owned `/var` alias. A real filesystem alias capture/reconcile test reproduced the rejection. | Confirmed storage defect, repaired by resolving the trusted SDK root's parent and preserving strict owned-subtree symlink checks. Whether this was the first device error needs new diagnostics. |
| R1 → R2 | First log: Force Clear approved at 00:47:31; next map 64700228 selected at 00:49:56; later map 150368604 downloaded at 00:51:19. | Server selection/download did proceed. Client outcome choice was match-scoped; navigation returned early before retiring old round UI. Both confirmed code defects are repaired. |
| Mid-entry native world | Requested 5%, source 4077 ms. At 00:53:36 the native probe reported 159 held clocks and zero command/spawn/follow/collision calls. Later calls advanced again. | Source start/reset gates were absent from recorded packets; the observer could apply history while the native world was still held. New verified source gates are carried to the authorized dummy. Actual moved-object parity needs GD verification. |
| Repeated restore | At 00:56:53 sequence 353 was refused repeatedly in the same Attempt/generation, with distance 119.0221. | The correction bound is 400, so distance alone is not the explanation. A present but invalid native snap proof can also cause rejection. The log lacks that frame's endpoints/modes. Invalid immutable evidence is now quarantined once and diagnosed precisely, rather than causing endless epoch resets. |
| Spider proof coalescing | Source policy combined native snaps, but the old proof required a net gravity flip. | Confirmed code defect: two valid flips can cancel between captures. An ordered bounded native proof chain preserves them; failed native jumps are not represented as successful snaps. This does not prove the exact sequence-353 event. |
| Audio | Source/native position differed during seek; later attempts logged native music position zero. | A void asynchronous seek was treated as success. The real channel seek result and readback now gate resume. Exact device clock/audio parity remains unverified. |
| Random | Pinned GD 2.2081 inline `RandTriggerGameObject::getRandomGroupID` uses `GameToolbox::fast_rand_0_1`. | Layer RNG seeds alone do not capture that decision. Record actual native output groups and replay them only in read-only worlds. |

## Replay storage and official reconciliation

`SurvivalReplayRecorder` logs `stage`, `result`, the first invalid metadata field or native sample, Attempt ID and round key. Worker errors retain their source identity; an old failure cannot overwrite the next Attempt's UI. UI distinguishes preparation/data/space/file failures, official confirmation waiting and unavailable matching native data. Capture timing now includes recorder activity, frame capture and tick; it is not a file-success signal.

File format 1 and existing atomic promotion stay intact. Optional `nativeWorldEventVersion=1` and typed `random` records extend the input timeline. Source output events carry a stable initial object ordinal/ID/position, activation ordinal, native tick/time and bounded native spawn outputs; no native pointer or secret is serialized. Playback validates identity and event consumption alongside native steps, anchors and poses. Missing or mismatched event evidence produces DESYNC rather than fabricated branches.

Receipt responses distinguish timeout/disconnect, 401/403, 404, 429, 5xx and 200/pending. Eight bounded retries preserve durable pending files, then defer until session renewal or explicit history/retry activity. There is no added idle DB loop. The actual receipt callback is tested through error classes and successful promotion on the real storage implementation. Approved Force Clear finalizes captured input with `DEBUG_FORCE_CLEAR`; an official 100% without matching native input cannot become a synthetic clear replay. Existing valid best files remain protected.

## Round-scoped navigation

Outcome choices belong to `matchId:roundKey:mapGeneration`. Legacy enum values remain unchanged; `Standings=4` is additive. Live Spectate still requires an official cleared round. During a match, eliminated players choose Free Play or Standings; final results require `FINAL_RESULT`. Observer exit returns to Standings. New map context retires old navigation, queued launches, target/world preparation and spectator requests before an early outcome return, without discarding the independent official event FIFO or pending replay reconciliation.

## Debug restoration, clocks and presentation

The new optional `restoreBarrier=1` / `readySequence` handshake freezes the recorded source time at the requested start while the viewer applies bounded historical pages. Pages read validated recording data, so a long restore does not expire the live relay and invent a new generation. The source resumes in the same generation only after exact-head world and audio readiness. Legacy HTTP v2 clients retain their path.

`worldFrame()` is internal; `presentationFrame()` hides pose while restoring. No visible Remote Player is updated during catch-up. Debug HUD distinguishes PREPARING, RESTORING WORLD, CATCHING UP and READY TO PRESENT from LIVE/BUFFERING/PARTIAL SYNC. Source native simulation time owns new recordings; renderer/network wall time does not advance a paused source. An exhausted queue can complete on a later caught-up response.

Recorded track hash and actual audio position, never percentage, determine song seek. Native audio warms paused during restore. The channel's seek and readback must succeed before unpause and the server presentation ACK. Missing or incompatible tracks are explicitly partial and remain silent. Native Song Trigger execution may select the source track; arbitrary unavailable tracks are not invented.

Terminal received during restore cancels the obsolete queue and separates death from clear. New Attempt/generation/epoch fences old callbacks and completions. Invalid immutable pose/snap evidence stops that context without busy retry; low-frequency polling can still discover a new authoritative Attempt. Old recordings without sufficient evidence require recapture; they are not silently repaired by jumping to a late pose.

## Native Random and Follow

`SpectatorNativeWorldEvents` observes actual `RandTriggerGameObject::triggerObject` and scoped `GJBaseGameLayer::spawnGroup` outputs. Normal official gameplay still invokes native RNG and spawn exactly once with original arguments. Only local input playback or an authorized remote dummy consumes recorded outputs. Source events are bounded and validated in both C++ and TypeScript; the Debug HTTP batch uses a 48 KiB conservative prefix and ACKs only sent frames. No new always-on world event upload is introduced for all 64 participants.

Debug RECORD captures the real 200-entry Follow Player Y history, index and remainder. The WorldDriver restores that ring at a new packet boundary, while legacy sources use the existing time sampler. Opt-in **WORLD TRACE** logs actual trigger/group activation and moved native object coordinates after native Follow processing, with role, tick, elapsed time, dummy/history sample and bounded counts. This supplies object-level device evidence; copying a ring alone is not claimed as proven Follow parity.

Survival live spectators continue to drive the world from their own clear input replay and render one selected remote target. Their own Random results and the target's potentially different Random results are distinct. Cross-player timing, path-dependent worlds and Player2-specific native triggers remain PARTIAL SYNC; ordinary Solo and official play retain their existing ownership and no-save protections.

## Validation and release evidence

Local production-path tests cover real storage alias/symlink rules, pending/best preservation, receipt classes and Force Clear mismatch; native input/Random output routing and exact codec validation; source clock/audio readiness; 0/1/5/20/50/100% HTTP start barriers with a 60-second held source; terminal/new-Attempt fences; repeated invalid evidence; and actual round-scoped outcome methods. Existing tests and strict C++ domains remain required. PostgreSQL 16 is isolated from Production.

Platform build, immutable R2 byte/hash/manifest/native-hook verification and actual Singapore activation are separate release stages. The production rollout record supplies their final results; this implementation document is not deployment proof.

Physical iPad/Windows GD tests remain required: immediate official entry with first-error diagnostics, ordinary/timeout/real-clear replay saves and history playback, Force Clear explanation, R1→R2 from board/Free Play/spectator, all requested Debug start points and audio, repeated Random/Advanced Random results, moved Follow X/Y/Player Y objects, Teleport/Spider/Reverse/Dual boundaries, and death/clear during restoration. No device, FPS or arbitrary-map parity claim is made from native harness or CI success alone.
