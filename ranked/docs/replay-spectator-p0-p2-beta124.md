# beta.124 replay / spectator P0-P2 integration

Start main `4127b46913e15ea77f74b72f0f981fc6c8d1515d` (beta.123). Status was
derived from the code, not from older documents. "Existing" means the item was
already implemented and only verified or extended.

| Item | Result | Code evidence |
|---|---|---|
| P0-A traffic separation | New + existing | Client queues were already separate (`SurvivalRuntime` events, `SpectatorLiveRuntime` publish/terminal/view holders). Fixed: Solo `SpectatorLiveService` called `MatchService.state()` (match row `FOR UPDATE` + heartbeat write) on every publish/poll; now a per-session single-flight read reused for 250 ms (`SPECTATOR_AUTHORITY_CACHE_MS`, `match.module.ts`). Survival FIFO overflow evicts PROGRESS, never Start/End (`SurvivalRuntime::enqueue`). `SURVIVAL_NO_VIEWERS` 409 no longer counts toward the 12-failure publish budget. Native capture drops log `CAPTURE_DROPPED` (`PlayLayer::noteSpectatorCaptureResult`). |
| P0-B classification / loops | New | `domain/SpectatorFailureFingerprint.hpp`: reason classes and a bounded (64) ledger keyed by channel, match, context, Attempt and generation. It survives `resetViewer`/`requestSceneRestore`; `applyViewerResponse` suppresses the same identity. HUD text localized (`spectator-*` keys). |
| P0-C first divergence | New | `Playback::diagnosticsReport` (32 verified frames before, 8 Source frames after, stage counts, category from `replayFailureCategory`). Random failures carry `$.random.CAPTURE_DROPPED / IDENTITY_UNAVAILABLE / HOOK_NOT_CALLED`. `Storage::exportDiagnostics` writes at most 10 files of 256 KiB each. |
| P0-D end/save | Existing + tests | Recorder/Storage flows unchanged. Added tests for real disk-full (Space notice, capture damaged, best kept) and an immutable closing journal. |
| P1-A demand / mid-join | Existing + UI | Demand gating already existed. Survival intentionally omits `worldHistory` (its world comes from the viewer's verified local replay). Partial sync now distinguishes truncated history from the declared native-replay limit. |
| P1-B restore entry | Partial | Restore progress `applied/total` is shown. History prefetch before scene creation was not added, because the restore timeline is scene-owned; the order is unchanged. |
| P1-C jitter | Measured, kept off | A/B test in `SpectatorInterpolationBufferTest.cpp` gave equal starvation/rebuffer and +70..330 ms delay, because the render clock is 1x once started. `setAdaptiveJitter` stays false by default; p95/max metrics are collected. |
| P1-D seek | New | Seek generation, target/progress HUD and newest-target coalescing. No mid-replay checkpoints: native world/RNG/CBF state is not restorable with proof. |
| P1-E music | New | Replay uses `SpectatorAudioClockPolicy`: one alignment after pause/seek/speed change, then 60/180 ms hysteresis with a 2 s minimum interval. Drift, seek and failed-seek counters were added. |
| P1-F history states | New | `replay/SurvivalReplayQuality.hpp` plus `Storage::inspectState`. Eight states; the button is enabled only when the replay is playable. |
| P2-A traffic size | Measured, not enabled | Synthetic baseline is ≈441 B/packet, ≈8.6 KiB/s per viewer, gzip 7%. Client gzip decoding cannot be verified here, so wire, codec and compression are unchanged. |
| P2-B compatibility | New | `ReplayCompatibility` (SUPPORTED / UNSUPPORTED_SETTING / VERSION_MISMATCH / MISSING_NATIVE_EVIDENCE / HOOK_UNAVAILABLE) is mapped from the existing exact codes and shown in history. |
| P2-C multi-viewer | New | One shared stream per target. 16 viewers × 5 polls cost 16 locked reads. Solo history has a 4 MiB byte budget. `diagnostics()` counters were added. |
| P2-D debug tools | New | The Debug HUD shows delay, p95, starvation, RTT, class/reason, sequence/epoch, restore progress, ledger and audio drift. Diagnostics are exported automatically on desync. Timeline markers were added. A native single-step mode was not added, because the CBF boundary cannot be proven. |
| P2-E retention | New | Header-damaged pending files are quarantined (newest 8; older-than-14-days excess removed); stale `manifest.next` is removed. Bests and decodable pending captures are never deleted. |

All results come from automated tests and native doubles. Behaviour on an actual
GD 2.2081 / CBF 1.5.0 device (playback, audio drift, HUD layout and markers) is
**UNVERIFIED ON DEVICE**.
