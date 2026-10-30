# Replay and HUD investigation (beta.118)

Baseline: main `2ea33c6d1057f5f054928d095c1dbf710049a7c3` (beta.117), fetched before work.
The original `Geode 2026-10-29 23.16.41.log` attachment path did not survive the
environment replacement. Reattachment was requested. Values reported by the user
remain reported observations until the original bytes can be inspected.

| Area | Confirmed code evidence | Interpretation / remaining evidence |
| --- | --- | --- |
| Initial physics | Replay initial records contain pose, RNG and a few clocks; `resetReplayInitial` restores clocks/RNG but no P1/P2 velocity, button holds, jump buffer, gravity or Ship rotation. | Confirmed incomplete restoration. Original jump/Ship incidents still require their original log and native execution for attribution. |
| Input queues | Every recorded `buttons` event stores full before/remaining queues. Playback clears and replaces the queue before native consumption, then compares remaining queues. | Clearing alone does not establish loss: recorded retained commands are reintroduced. Removal from a queue also does not establish a jump was applied. Need actual handleButton and physics evidence. |
| CBF | v1.5.0 public source moves commands into private inputVector/stepQueue, splits PlayerObject update/rotation and uses render-clock timestamps; its soft toggle does not disable physics bypass. | A queue-only GD replay omits that external state. Loaded status alone does not prove the setting was active in the reported incident. Unsupported active timing must be explicit unless exact reproduction is implemented. |
| Native step | Current codec validates absolute nativeStep and records only before processCommands. | User-reported source 364 / playback 0 requires per-render/per-physics boundary traces; do not invent an offset or widen pose tolerance. |
| Solo score | Shared ScoreDisplay truncates ordinary values; GameplayUx truncates both scores before subtraction. | Confirmed loss of fractional score and differences in presentation. Server scoring must remain unchanged. |
| Survival DM HUD | DM uses a single .72-scale white combined label while ordinary rounds use separate CompetitiveClockScale labels, measured pair placement and standing tint. | Confirmed divergent styling. Left FPS-relative timer remains unchanged. |
| Collection totals | The displayed difficulty total sums all six grades. | ELIMINATED must be excluded from ordinary difficulty totals per the new instruction and retained in its separate collection. |
| DM badges | `survivalRoundBadges` maps FINAL and every TIEBREAK to one FINAL key and label. | New instruction requires one official badge per actual round, labeled FINAL/DM1/DM2, source-round deduplication and bounded UI for repeated DMs. Existing award evidence must be preserved. |

## Public references (research only; no incompatible implementation copied)

- ToastyReplay `016a5c8219e39c732583f655583ceee1982a3168`: PolyForm Strict 1.0.0 with personal noncommercial modification permission; redistribution of derived builds is not granted. Inspected replay_engine.cpp and checkpoint_handler.cpp for boundary/state categories, not copied into Corum.
- GucciBot `f6ad752d31bf0ca7e954961d7756e2fdbe9da9cf`: GPL-3.0. Inspected queue/native dispatch separation and timing diagnostics; no implementation copied.
- xdBot `16ef8e86d3a295119583a1c236be7a9aab7568e4`: no license file/declared repository license found in the inspected tree. Inspected original input records and frame ownership; no code copied.
- Click-Between-Frames `9fa7c6f590945d048b8e83de71eaba30dcb0c64e`: MIT, mod v1.5.0 / syzzi.click_between_frames. Confirmed soft-toggle, physics-bypass and split update/rotation paths from its actual source. Corum changes will use independent code and the public GD/Geode bindings.

Detailed implementation, tests and actual-device limitations will be appended with
the evidence gathered. DESYNC checks and official gameplay/score isolation remain
required throughout.

## Implemented boundaries

- New native input version 2 preserves the existing file format and hash chain.
  An initial physics schema captures P1/P2 velocity, Ship rotation, gravity,
  controls/holds, buffering, dash, slope, game modes, transforms, RNG and native
  clocks/CBS flags. All required data and both players' map-object reference
  identities are validated before one initial restore. No frame pose is applied
  as a correction. Unresolved source references are refused.
- The complete frame-start input queue is available **before** native update and
  its scheduler. Pending commands from the previous native frame must match in
  both recorded and local queues. Only the recorded new suffix is injected;
  carried commands are not cleared/reinserted in each physics callback. Legacy
  decoding remains separate. Direct between-frame handleButton dispatches are
  captured and applied once before the next native frame.
- Queue consumption and actual handleButton dispatch/held state are separate
  evidence. Frame poses include compact motion, native clocks, RNG, outstanding
  commands and actual PlayerObject update counts. The existing player safety
  hook supplies the last eight actual before/after physics samples for the first
  failure log without adding another PlayerObject detour or persisting a large
  trace on every frame. First failure keeps frame/step, previous expected pose,
  expected/actual input and physics data, actual before/after and loaded mods.
- Physics callback accounting runs only while the recorded world advances;
  ordinary native scene initialization/reset cannot consume tape steps. Restart
  resets playback accounting before validating/restoring the initial world.
- Native frame dt is still replayed unchanged. Render speed changes the budget,
  never the stored native dt. Existing pose, rotation, anchor and Random checks
  remain enabled with their original tolerances. No official Attempt, score,
  LP/AP, achievement or global RNG is generated by replay.

## Explicit compatibility boundary

CBF v1.5.0 moves the GD queue into its own private input/step queues and splits
PlayerObject update/rotation using its own clock. No portable public API for
capturing/restoring that private state was found. Queue-only re-injection is not
an exact implementation of active CBF. Active CBF, CBF physics bypass/COS,
unverified CBF versions and missing settings are explicitly unsupported.
Verified v1.5.0 disabled with bypass/COS off, or CBF absent, permits the native
boundary path. No CBF setting, installed mod or official environment policy is
changed. Existing official policy requires active CBF, so new official captures
in that environment remain unavailable until exact CBF replay is implemented;
this release must not be described as verified CBF replay support.

Old files still decode and existing bests/files/official histories are preserved.
A legacy file without the required initial physics/environment evidence is
explicitly unavailable for native playback; missing velocities/holds are not
guessed. An unsupported or damaged new capture cannot replace a valid best,
including when its Start ACK arrives late. Spectator admission keeps the own-clear
world + one selected opponent policy and refuses an unsupported native world
rather than inventing LIVE playback or reporting network DISCONNECTED.

## Other integrated fixes

Ordinary Solo score labels and raw differences use one decimal without changing
server scoring. Survival DM shares the ordinary round's font scale, measured
clock/delta pairing and rank tint; only its remaining-attempt numerator replaces
the central clock. The server timer stays in the existing FPS-relative top-left
row. Collection difficulty totals exclude ELIMINATED, with the separate X count
retained. FINAL and every real TIEBREAK receive distinct FINAL/DM1/DM2 labels and
source-round identities. Existing ledger rows remain unchanged; a unique source
index prevents duplicate aliases while verified missing rounds are backfilled.
Personal result cards page within the existing result panel, and existing paged
history tabs expose all repeated DMs.

## Validation limits

Production-helper C++ tests exercise queue ownership/order/timestamps/P1-P2,
initial-state serialization, actual recorder/filesystem ACK handling, CBF profile
classification, exact callback accounting, pause/restart/seek and 0.5/1/2/4x
budgets. These contract tests do not execute GD's proprietary physics. No real
GD 2.2081, Cube jump/Ship/Dual/portal playback, CBF ON/OFF game run, device HUD
inspection or audio listening was available in this environment. The original
23:16:41 attachment path was absent after environment replacement; reattachment
was requested. Reported values remain user-provided evidence, not re-read logs.
The exact incident attribution therefore remains unconfirmed.
