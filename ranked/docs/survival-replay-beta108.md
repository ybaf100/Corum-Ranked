# Survival local input replay and cleared spectator — beta.108

Baseline: `8d6cc3ee4478cb63db4db8e4597b0572392ee771` (beta.107). Production deployment is a separate recorded stage; this document does not certify a physical device.

## Native capture and local authority

`src/replay/SurvivalReplayNative.cpp` observes actual GJBaseGameLayer update, processCommands and consumed PlayerButtonCommand queues. It records P1/P2 press/release, native step/timestamp, outer dt, initial named RNG seeds/clock state, native Teleport/Spider anchors and pose/camera/progress checks. It does not synthesize input from percentage. Start creates bounded provisional RAM; only a server Start ACK or owner-only receipt commits it. Free Play, practice, history playback and live viewers never enter this recording branch. Death/reset/clear/quit, round return and session/context retirement finalize the capture, deferring a terminal raised inside physics until its final pose. Duplicate terminals are idempotent.

The existing official scoring FIFO remains authoritative. The read-only receipt endpoint `/api/ranked/survival/replays/matches/:id/attempts/:attempt` reads only the requesting participant's official state. It returns no replay bytes and performs no scoring writes. History projects only the owner's first highest scored ended Attempt and exact map identity. Ordinary scores truncate; Tiebreak uses decimal milli-percent, including equal-score ties. The first actual 100% may replace a rounded non-clear tie.

## Format 1 and storage

The mod save directory contains `replays/manifest.json`, `pending/<sha256(attemptId)>.tmp`, and immutable `survival/<round-identity-sha256>/<attempt-sha256>.replay`. Raw IDs remain in metadata; path components are derived hashes. The round key includes match, round, generation, participant and Debug/online partition. Symlinks are refused.

JSONL contains a versioned identity header, ordered blocks of typed native records, and a footer. Each block commits to the previous digest and its canonical record payload. The footer contains payload SHA-256/byte count, terminal, counts, elapsed duration and completeness; its own SHA-256 protects summary corruption. Metadata includes game/Geode/mod version, map geometry SHA-256/version, Attempt ordinal and start time. The manifest stores the server-confirmed best separately from locally captured progress. No token/password/native pointer is serialized.

A serial IO worker owns JSON decoding, checksums, flush/fsync, atomic replacement and replay indexing. The queue is bounded to 128 tasks/2 MiB serialized charge, a file to 48 MiB/250,000 records, a block to 256 records, and pending storage to 64 files/128 MiB with 16 MiB free-space reserve. Filesystem directory-sync unsupported errors are tolerated only for directories; atomic rename still applies. Android/iOS durability and costs require device testing.

Promotion verifies a complete file and exact closed official receipt, writes an immutable candidate, commits the manifest, then removes the superseded best. Ties/lower scores and IO failures preserve the existing best. Recovery truncates an incomplete trailing block and recovers a rename-before-manifest journal window. A prefix can be sealed only when its captured score matches an ended official receipt; it remains `PARTIAL_RECOVERED`, retains its actual captured progress, and cannot authorize a live spectator world. Unverified captures are never promoted. There is no silent best-file eviction.

Hashes identify corruption and map compatibility; they are not proof that a client did not fabricate evidence. Reporting, uploads and automatic submission are absent.

## History playback and spectator world ownership

History has a device-local replay button, including disabled missing/damaged states. The loader checks the official Attempt and exact saved map/hash/version, then uses existing native download/song/scene-return owners. Actual matches/queues/native scenes block admission. IO futures own data only; callbacks fence the exact scene and cancellation.

Playback re-injects recorded queued input into the native simulation, checks processCommands steps and P1/P2 pose anchors, and stops on `REPLAY DESYNC`. It offers pause, restart, 0.5/1/2/4x, time, slider and ±10s. Seeking resets and simulates forward, capped cooperatively at 96 frames/3 ms per pump; one heavy native frame can exceed this budget. Native GJBaseGameLayer owns postUpdate dispatch. The full simulation is in an exact-owner/no-save scope. Music is paused during seek/non-1x/held states and uses recorded song position at 1x where available.

Server Survival viewer admission now requires current-round participation, official ended 100%, no active Attempt, PLAYING, a valid session and a different live target. Eliminated users retain Free Play/results and cannot reach live watch through the board, HUD or API. The client additionally requires a matching COMPLETE locally preserved clear replay. Bot percentages never fabricate native streams.

Only the selected remote target is subscribed/rendered (two avatars for its Dual pose). The viewer's own input replay drives the native world; the remote WorldDriver/history does not also fire triggers. Remote pose/cosmetics/effects drive render-only avatars and camera. Native replay camera state is restored before simulation and remote camera applied after, preventing presentation feedback. Target/Attempt/map/round changes reset the owned world and reject old packets. Free Play/history/live state and teardown remain separate.

**Limits:** a different player's path, timing or inputs need not produce the same world at the same percent. This model always discloses `PARTIAL SYNC`. Complete arbitrary engine object/RNG/action serialization, external-mod parity and every trigger's physical-device determinism are not certified. Pose/step validation is not full object-state validation. No physical GD playback/seek/clear-animation timing is certified by the portable tests.

## Debug and follow-up evidence

Debug RECORD accepts an exact positive signed-32-bit Level ID, defaulting to VOICE. TEST ignores the record input and requests the latest completed supported-format recording, ordered by finalize time across maps. The server returns that recording's exact map ID; metadata, LevelInfo preparation, viewer context and packet validation all use it. Incomplete recordings are not TEST candidates. Existing opt-in/header/password/session/scoped tokens, native resource verification and no Ranked scoring remain.

Migration `0031_debug_spectate_custom_maps.sql` replaces only the isolated debug table's fixed-VOICE CHECK with a valid Level ID range and adds the latest-finalized index. It retains every existing row and is idempotent. It does not reset achievements or touch Solo/Survival LP, Attempts, profiles or production settings. Existing older servers remain compatible with the relaxed constraint.

New spectator samples carry actual native song position and a hash of the resource basename, never local paths. Viewer audio waits during history restoration, seeks the recorded source time once ready, and pauses on buffering/terminal. Legacy recordings without source audio and unavailable song-trigger tracks are disclosed as unsupported, never mapped from percent. The server abandons obsolete long catch-up when terminal arrives and resets cleanly for the next Attempt. Debug logs distinguish requested start percent, playbackStartElapsedMs and actual music time.

History reserves a separate footer/safe viewport, stacks map/own record/roster on 4:3/narrow screens, lays out rank/icon/current name-title/attempt-result/BEST explicitly, and resolves only visible GD icons. Current profiles/titles refresh on explicit reopen/refresh; no historical title snapshot is invented. History has no persistent settings overlay. Leaderboard narrow columns/footer and actual bot icons are aligned; Tiebreak shows clamped remaining starts as the main HUD and time at upper left.

The existing native Follow X/Y command paths and their Debug probes are preserved. The provided screenshots/instructions do not establish a native Follow root cause, and no manual object-transform workaround was introduced. Follow/Follow Player X/Y, mixed trigger maps, audio latency and the UI screenshots still require iPadOS/Windows reproduction and inspection. Portable command-order tests and package builds must not be described as that validation.

## Verification and release

Portable storage/recorder/playback, admission/authority, source-audio, terminal restore, layout/tap/virtualization and prior safety regressions run against production implementations with explicit SDK/network/native boundaries. PostgreSQL tests use an isolated local PG16 cluster; no production-data test runs. Final counts and platform/publication/activation evidence belong in the rollout record after completion.

Publish both immutable Release/Debug packages, download and validate full bytes/hash/native resources, then activate this source on Singapore Lightsail with the existing rollback and version barrier. Preserve account/LP/AP/history/achievement/migration data and all secrets/settings. Railway/Render are not targets.
