# beta.112 investigation (2026-10-28)

Baseline: origin/main e040c9c18ae06ccab4bbecac8fa5055dde7b04cb; clean checkout, fetched at start.

All three user-supplied Geode logs have now been read. `22.03.07` (SHA-256 a098781375dc1edc4e5bd6bf3a7253c08dc3e1841916e9c8ba066ef57c3e1775) contains 2848 lines: **61 partial state samples**, **733 SEEK_APPLIED**, **293 seeks within 25ms**, **11 all-zero seeks**, **9 pending-presentation seeks**, peak **27 seeks/s** at 22:03:57. There are zero SpectatorRandom logs and zero replay DESYNC diagnostic records. The two earlier startup logs (541/644 lines) contain none of those events. No GD executable or physical device is available here.

The first TEST starts 22:03:42 using attempt ea77b64d…; the second RECORD 7f93dba0… starts 22:04:25, finalizes 22:06:06 and is replayed from 22:06:11. Thus the absence of Random logs also affects a fresh RECORD, but does not establish which hook/identity prerequisite failed. Persisted source frame evidence was subsequently located by read-only production queries (see below).

| Problem | Evidence available | Code path / confirmed defect | Still unproven |
|---|---|---|---|
| Random missing source activation | Log confirms repeated missing-source state | NativeWorldEvents remote hook suppresses unverified activation; debug capture silently drops beyond 16 pending outputs. Playback also invokes local RNG before replacing outputs, changing local RNG state. | First missing RECORD/server/TEST event; GD 2.2081 basic/advanced hook coverage; actual group coordinates |
| Sticky PARTIAL SYNC | 61 recounted state samples (not 61 events) | noteWorldPartial sets one sticky boolean, reset only by stream-level paths; no reason-specific repair | Which causes occurred and whether source evidence later recovered |
| Audio seek storm | 733 recounted applied seeks | syncSpectatorAudio seeks whenever paused, even when requested/actual == 0; presentation barrier keeps paused true, repeating seek; pause called each frame | FMOD clicks/ownership/track behavior and measured post-fix seek totals |
| sequence 353 | 22:04:03: SPIDER→SPIDER, gravity true→true, SPIDER_SNAP,119.0221,nativeEvents=0 | Legacy snap validation requires adjacent gravity/mode change without native events; same-poll multiple flips can invalidate this evidence | Exact native movement, capture and codec loss; cannot declare legal or illegal |
| sequence 1443 | 22:07:25: SHIP→SHIP, gravity false→false,NONE,1455.0317,nativeEvents=0 | Unproved large corrections correctly fail closed | Native teleport/move/packet order; no threshold relaxation authorized |
| DISCONNECTED | Code | status returns Disconnected for rejectedWorldContext, conflating world evidence failure with transport | Actual network history in supplied log |
| FPS/timer | Code | top-.5 and top-3 anchors collide; DM attempts and delta rendered separately | Physical device safe area / font bounds |
| 3 attempts | Code | rules, bot and client hard-code 3 despite snapshot config | Existing production live match count now |
| No exhausted return | Code | reset path pauses on attempt limit; survivalFrame only auto-returns clear | Physical iOS native teardown |
| Jump replay desync | Code | Consumed commands are injected at frame start, not original processQueuedButtons boundary; per-frame clear can discard retained queue. No input-consumption diagnostics; only boolean desync. | First failing native field in user's replay; full initial physics parity |

Changes must preserve pose tolerances and deterministic validation. Missing evidence must remain visible and held. Automated native doubles do not establish physical GD correctness. Update this document with actual tests and limitations before publication.


## Implemented boundaries

- New match admission captures six attempts, old serialized match configs retain three. Scores, early completion, forfeit completion, live view and bots read the snapshot. Migration 0034 only expands Survival projection sequence bounds; no record/account/LP mutations.
- The native replay bridge stores exact pre/post-dispatch button queues at each `processQueuedButtons(dt,clear)` call, preserving step, timestamp, push/release and P1/P2. New playback validates dispatcher identity and remaining queue. Legacy replay reading uses recorded tick boundaries and preserves pending queues. Seeds/timers/nativeStep and the additional native fractional `extraDelta` are restored; full GD physics parity remains unverified.
- First replay failure reports a typed reason, frame/step/dispatch count and expected/actual state. Coordinate/rotation thresholds are unchanged. No prior replay file or best record is edited.
- Audio alignment is independent from pause state: zero drift does not seek. Advancing presentation samples must show >=180ms error for >=750ms, with >=2000ms between corrections and a <60ms recovery band. Pausing/resuming happens on transitions. This policy test is not a listening test or a measurement of modified GD.
- Random identities are built at the actual first capture activation, not only during init. A 256-event capture backlog emits <=16 events/packet, rather than dropping every activation beyond 16. Reapplication uses verified native spawn outputs without invoking local RNG, retains trigger/control IDs where captured, and gates remote dispatch on source due events. Unmatched activations retain a cause. Actual entropie branch/object parity is still unverified.
- Cause recovery removes only matching transient debt; missing history/input/Follow proof remains partial. Rejected immutable pose contexts are quarantined with RECAPTURE REQUIRED; real transport retry exhaustion remains DISCONNECTED. Last validated rendered state is held.
- DM HUD has one remaining/limit + signed 3-decimal difference line. The auxiliary timer uses the rendered FPS bounding box and reserves roster space below its font height. Exhaustion return requires the same native scene context, no local active attempt, server attemptsFinished >= snapshot limit, no server active attempt, drained Attempt End queue and completed native Clear teardown.

## Explicit unresolved evidence

Neither log contains a first jump DESYNC record, actual replay bytes, or source Random selections. Persisted source poses narrow the two rejected sequences, but the underlying Native callbacks remain unproved. These exact incidents cannot be declared device-fixed from the available evidence. No corrected-client FMOD recording/listening, physical iOS/Android/Windows gameplay, or exact Group/Object coordinate comparison has been run. Newly added diagnostics and deterministic harnesses are evidence for code paths only.


## Persisted source evidence (read-only production query, 2026-10-29)

- Recording `ea77b64d-a7c6-47a6-acd7-fa5e5d18e5fe` was created **2026-10-27**, before beta.111: 790/790 frames persisted, no Random events and no native chains. Sequence 352 is (5891.456,3756.900), sequence 353 is (5895.642,3637.951) with a legacy SPIDER_SNAP and unchanged gravity. The stored legacy recording lacks the chain required to prove net-cancelling native gravity changes. It is held for recapture rather than rewritten or accepted by relaxing validation.
- Fresh recording `7f93dba0-f8a0-4a0c-aa98-acd0854e97cd`: 1995/1995 persisted frames, 30 native-chain frames, **zero Random-bearing frames**. This proves the absence exists before TEST application; it does not distinguish failed identity prerequisites from a native Hook path that never ran. Capture-ready/identity rejection diagnostics now distinguish those cases.
- Sequence 1442 source: elapsed 71279ms, SHIP, (25882.998,3984), camera (25717.998,3852). Sequence 1443: elapsed 71295ms, SHIP, (25892.600,5439), camera (25727.600,5307), no discontinuity. The source itself contains the unproved relocation. RTT alone is not used to diagnose or dismiss networking.
- No production row was inserted, altered or deleted during this investigation. Production schema is changed only by the existing deployment migration runner.

## Validation before main activation

- Rules: 342 passed. Local Native suite: 317 passed. Full server suite: 700 passed plus the existing optional pool test skipped in that invocation. PostgreSQL 16 HTTP: 65 passed. The omitted pool reconnect test separately passed on a dedicated local PostgreSQL database; no remote backend was terminated.
- CI caught a dangling temporary queue reference; the production loop now holds a local array value. CI also caught the old Apps Script three-attempt expectation; it now verifies six. Apps Script's four tests and the targeted replay bridge pass locally.
- PR #36 Validate and all four Release platform builds passed. All four Debug ON and four Debug OFF builds also passed before main activation. Build success does not establish GD device behavior.
- Migration 0034 applied twice in isolated Neon PostgreSQL 18 branch `br-young-hill-azqqa6gp`, copied from production. Both applications preserve 1 account, 1 Survival profile, LP 110, AP 45, MMR 150, 2 Survival matches and 198 existing TIEBREAK attempts. The test branch expires 2026-10-30. Existing live snapshots remain authoritative at their stored limit.

- Pre-activation review added an actual Recorder-to-storage regression with a nonempty button queue. Nested commands carry button data; tick/elapsed belong to their enclosing dispatch. The codec now validates that structure without erroneously requiring top-level provenance on each nested button. All four replay bridge/playback/recorder/storage harnesses passed after this correction.
