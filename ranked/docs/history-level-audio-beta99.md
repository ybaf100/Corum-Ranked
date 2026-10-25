# History level audio handoff — beta.99

## Source and retained operator changes

The authoritative base is `0ccf28892c2c6c278195597b769e9c014005ee99`.
The user's three beta.98 publication/activation/report commits are retained,
including [the verified beta.98 rollout](beta98-production-rollout.md).
Existing immutable beta.98 packages are not republished or overwritten.
An independent worktree preserves the older unpushed beta.98 staging area.

## Fix

History opens the exact recorded level in ordinary Geometry Dash LevelInfo.
That ordinary PlayLayer correctly never claims Ranked gameplay ownership, so
its audio handoff cannot clean up the separate Ranked FMOD channel. Previously
history left the lobby channel audible through LevelInfo and normal playback.

After successfully constructing both LevelInfo and its transition, history now
uses the existing immediate native audio handoff. It cancels pending audio starts
and stops active, retiring and standby Ranked channels without stopping native
GD music or sound effects. Failed or cancelled lookups keep lobby music playing.
An already pending scene transition cancels the lookup before any audio handoff.

The retained lobby requests BGM only while it is running in its own current scene
with no pending scene transition. This fences outgoing fade callbacks and restores
the appropriate normal/promotion/demotion lobby track after returning. Existing
Solo and Survival gameplay music ownership is retained. Mode, match, round and
scroll selection stay in the retained history layer. No Ready, Ranked Attempt,
result, achievement or LP/MMR write is added to native history navigation.

## Validation

- Native C++ tests execute the production history transition and lobby audio
  callback, including exact ID, successful handoff, outgoing/incoming scene fades,
  return, lookup failure/timeout, scene creation failure and busy navigation.
- Production FMOD transition tests in Debug ON and OFF verify immediate active,
  retiring and standby teardown, no restart on resume/tick, native audio isolation
  and explicit lobby restoration.
- The supplied `Geode 2026-10-25 00.23.26.log` path was absent from this execution
  environment. This diagnosis is based on the current source and executable tests;
  no log inspection or physical iPad/iPhone/Android/Windows playback is claimed.

## Publication and activation

Synchronize the manifest, workspace/lock metadata and server client versions to
`v0.5.0-beta.99` (previous `v0.5.0-beta.98`). Publish Release and Debug with an
explicit publication commit that suppresses server deployment. Independently
download both complete packages and verify descriptor hashes, size, embedded
manifest/channel and all four platform binaries before activating Singapore.
Then deploy through the established Lightsail workflow and pinned SSH/rollback,
and check public health/readiness/config and the Railway update-only bridge.

No new schema migration, achievement reset, operator mode change, Neon setting
change, retired Seoul deployment or Railway gameplay writer is introduced.
This client-only fix does not measure or newly claim Neon Compute suspension.
Actual execution/publication/deployment results are appended below when available.

## Local checks before publication

- `npm ci --prefix ranked --ignore-scripts`: success.
- Workspace TypeScript typecheck: success. Version admission/controller tests: 37/37 passed.
- Translation consistency: 674 UI keys and 48 achievements passed.
- Focused native history/audio handoff: 4/4 passed (production FMOD Debug OFF/ON).
- The first full native invocation omitted SDK dependency variables, and the
  existing retained-scene test double needed the newly used scene APIs. Those
  issues were corrected; the five focused SDK/lifecycle checks passed on retry.
  A complete rerun uses the same pinned matjson/Result dependencies as CI.
- Preflight: both beta.99 descriptor URLs returned HTTP 404, leaving immutable
  publication targets unused. Singapore currently requires beta.98 and serves
  both beta.98 channel descriptors; prior operator deployment is retained.
- Read-only production baseline at 2026-10-25 01:49:32 UTC: 47 accounts including
  bots, 52 Solo matches / 150 Attempts, 94 Survival matches / 18,009 Attempts,
  22 achievement unlocks / 22 AP ledger rows, 395 profile and ledger AP. The
  overhaul epoch remains 2026-10-24 04:59:47.102 UTC; migrations 0020/0029/0030
  are recorded, native_death exists, LP ledger inconsistency count is zero,
  and the operator's 64-player mode remains enabled. No data is changed.

- Complete native rerun with pinned SDK dependencies: **287/287 passed**,
  zero skipped, 158.99 seconds. The earlier environment/test-double failures
  are resolved; no production behavior test is left failing.
- Ranked rules/server workspace build: success.
