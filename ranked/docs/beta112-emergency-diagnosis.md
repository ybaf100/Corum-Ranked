# beta.112 investigation (2026-10-28)

Baseline: origin/main e040c9c18ae06ccab4bbecac8fa5055dde7b04cb; clean checkout, fetched at start.

All three user-supplied Geode logs have now been read. `22.03.07` (SHA-256 a098781375dc1edc4e5bd6bf3a7253c08dc3e1841916e9c8ba066ef57c3e1775) contains 2848 lines: **61 partial state samples**, **733 SEEK_APPLIED**, **293 seeks within 25ms**, **11 all-zero seeks**, **9 pending-presentation seeks**, peak **27 seeks/s** at 22:03:57. There are zero SpectatorRandom logs and zero replay DESYNC diagnostic records. The two earlier startup logs (541/644 lines) contain none of those events. No GD executable or physical device is available here.

The first TEST starts 22:03:42 using attempt ea77b64d…; the second RECORD 7f93dba0… starts 22:04:25, finalizes 22:06:06 and is replayed from 22:06:11. Thus the absence of Random logs also affects a fresh RECORD, but does not establish which hook/identity prerequisite failed. Source/native frame dumps for sequence 1442/1443 are absent.

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

Neither log contains a first jump DESYNC record, actual replay bytes, source Random selections or native before/after states for the two rejected sequences. These exact incidents cannot be declared fully diagnosed or device-fixed from the available evidence. No corrected-client FMOD recording/listening, physical iOS/Android/Windows gameplay, or exact Group/Object coordinate comparison has been run. Newly added diagnostics and deterministic harnesses are evidence for code paths only.
