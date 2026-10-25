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

## Main push and external execution blocker — 2026-10-25

- Source commit **1192527c4a45e7e22d8000c94aab54db5d19a384** was pushed
  normally to main using the connected ybaf100 account, with the prior main SHA
  as a lease. All three operator beta.98 commits remain ancestors. No reset or
  force-push was used. The 13 modified/added files comprise the three audio/history
  sources, three native test files, synchronized six version metadata files and
  this report.
- [Release run 40152401206](https://github.com/ybaf100/Corum-Ranked/actions/runs/40152401206):
  failed before any job step; the manifest job has `runner_id=0`, an empty runner
  name and `steps=[]`. Builds, packaging and publication were skipped.
- [Debug run 40152401120](https://github.com/ybaf100/Corum-Ranked/actions/runs/40152401120):
  both initial policy/manifest jobs failed before any steps; platform builds,
  packaging and publication were skipped.
- [Source CI run 40152401116](https://github.com/ybaf100/Corum-Ranked/actions/runs/40152401116):
  failed before execution. One failed-job retry was accepted but attempt 2 again
  ended with runner_id 0 and no steps. These are unexecuted external checks,
  not passing CI or compiler/test failures.
- GitHub check runs contain two error annotations each, but the connected
  connector rejects the annotations endpoint and the job logs do not exist
  (404 BlobNotFound). No Browser tool is available to inspect the UI. The exact
  account/runner error text is therefore **unverified**; billing/quota must not
  be asserted as the cause without the GitHub error message. The operator was
  asked for the error text shown on the run page. No billing or runner settings
  were changed, and no publication protection was bypassed.
- [Lightsail run 40152401112](https://github.com/ybaf100/Corum-Ranked/actions/runs/40152401112)
  was intentionally skipped by the staged publication commit. **beta.99 has not
  been published or deployed.** Keep production on its verified beta.98 until
  both new immutable packages exist and are independently verified.
- Read-only DB recheck at 2026-10-25 01:57:07 UTC exactly matches the baseline
  counts, both full profile digests, AP totals/ledger rows, activation epoch and
  64-player operator configuration. No achievement reinitialization or reset
  was performed.

### Resume after GitHub runner execution is restored

1. Fetch latest main and preserve further operator changes. Rerun failed jobs
   on the original Release, Debug and Source CI runs above; retain the original
   publication event and source SHA. There are no beta.99 objects to overwrite.
2. Verify actual four-platform Release builds, all Debug ON/OFF builds, native
   tests and both publish jobs. Independently download and SHA-256/size-check
   both beta.99 packages, embedded version/channel manifests and platform payloads.
3. Only after successful publication/verification, create a normal main
   activation commit with the explicit Lightsail deployment marker and optional
   R2 publishing suppressed. Keep pinned SSH, Singapore check and rollback.
4. Verify the actual Docker build/upload/activation, live health/readiness,
   required beta.99 and both update channels on Singapore and the update-only
   Railway bridge. Repeat the read-only preservation comparison and record
   physical-device audio testing separately.

- Public recheck at 2026-10-25 01:57:09 UTC: Singapore health 200, readiness
  database/schema/ranked flags all true; required version and both channels
  remain beta.98 on Singapore and Railway. The operator's 64-player mode is
  ready/enabled. Both beta.99 descriptors still return 404. GitHub's public
  status reports all systems operational, which does not establish account
  runner eligibility. No successful beta.99 platform build or device playback
  is claimed.
