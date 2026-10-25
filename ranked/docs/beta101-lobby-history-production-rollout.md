# Corum-Ranked beta.101: lighter history, round-based cards and lobby population

Authoritative starting main: `7ebd8c60143f552c5836849357d4993aa9fc42b0`.
All existing operator work and the completed beta.100 rollout are retained.
Work branch: `fix/history-eliminated-round-beta99-20261025`.

## Client changes

- Remove the tier images and their ambient effects from history player rows.
  Keep current LP/promotion, equipped titles and player-info taps. The player-info
  popups and leaderboard retain their requested dual-mode ratings/title display.
  History's batched current profiles also omit the unused Survival record-summary
  aggregation: one profile query instead of two; no UI feature uses that summary.
- For **64-player** Survival, use compact cards from Round 1 preparation through
  the entire Round 2 elimination announcement; reduced BEST/NOW cards for Round 3
  after Round 2 entrants are removed, through its elimination announcement; full
  cards from Round 4 onward. The coordinator uses the presented completed phase
  during an announcement, avoiding a premature density change when the next
  server round already exists. Tied cutoffs do not change that phase policy.
  The 16-player format retains its full cards.
- All card densities use confirmed/live round placement colors: gold, bright
  silver, bronze, cyan for fourth/fifth, green for other passing ranks and red
  for nonpassing/eliminated players. Unknown/preparation ranks stay neutral.
  Recolor existing static border/highlight layers only when their color changes;
  do not allocate the previous three medal overlays on every card. Preserve
  stable card identity, movement interpolation, progress and profile input.
- Credits identify `hwanhee1` under `PROJECT LEAD · GAME DESIGN · DEVELOPMENT`.
  The AI-assisted implementation heading, Codex and explanatory line use smaller
  text. Music attribution, async real song metadata and the scaled popup menu
  remain. The taller logical canvas is centered and fits small/iPad viewports.
- The main title is `RANKED`. Its former mode subtitle is replaced by centered
  red-dot offline and green-dot online counts, formatted `100 / 50`. Dot radius
  is two logical units; labels recenter when digit widths change. Missing data
  shows `--`, not invented zeroes. The existing mode-selection cards remain.

## Population contract and idle behavior

The client sets `includePopulation=true` on its **existing five-second session
heartbeat** only while the Ranked lobby is the visible running scene (including
its mode/queue view). History, gameplay, scene handoff and scene exit disable
that flag. A late response cannot change counts after the lobby is hidden or
its session is replaced. No additional client HTTP loop is introduced and count
updates do not rebuild the lobby, restart audio or restart mode animations.

The authenticated heartbeat can return `population: {offline,online,sampledAt}`.
Registered positive-account human players form the total; bots/placeholder IDs
are excluded. Online means at least one non-revoked, unexpired session with a
heartbeat inside the existing 90-second admission lease. Duplicate sessions count
once; explicit Close releases a session, and crashes age out under the existing
lease. Offline = registered humans minus online humans, not total GD users.

The singleton SessionService batches concurrent readers and caches the count for
five seconds. The read runs after the admission transaction releases its player
lock. A failed optional read leaves a valid heartbeat valid. No background timer,
DB listener, keepalive query, migration, epoch/reset or Neon setting is added.
When nobody requests lobby counts there is no population SQL, even after cache
expiry. Server uptime and Neon compute suspension remain separate concerns.

## Verification and release

The focused tests execute production phase/layout, card update, heartbeat and
population widget code. They cover all phase boundaries, 64 visible cards,
elimination announcements, unchanged-node color reuse, centered long counts,
invalid/missing data, duplicate sessions, expired/revoked/bot exclusions, demand
cache/coalescing and closing/retired heartbeat callbacks. Actual device FPS,
audio/touch and full native gameplay remain device checks.

- Rules suite: 288 passed. Server full run before the final history-only query
  omission: 625 passed / one optional local pool test skipped.
- Session/population targeted suite: 19 passed. Native card/lifecycle/credits
  targeted run: 26 passed; extended heartbeat/card behavior: 9 passed; population
  widget/profile/phase focused run: 14 passed.
- Workspace typecheck, rules/server build, 679-key/48-achievement localization and
  11 release/package policy tests passed. Final full native and fresh source CI
  outcomes are recorded below after execution.
- Synchronize all six version metadata files to beta.101; previous version is
  beta.100. Publish both immutable channels before activating Singapore through
  pinned SSH and rollback. Use `[publish-r2] [no-deploy]` for the source push.
- No Production data reset or schema change; Solo OFF / Survival 64-player ON
  settings and the existing single Singapore writer are retained. Railway and
  Render are retired targets. Render is already stopped per the user; actual
  Railway shutdown still needs connector reauthentication.

See [beta.100 production evidence](beta100-production-rollout.md) for its verified
packages, activation, public API and preserved achievement/AP/LP data. Source,
package and actual server deployment are reported separately.

### Completed local validation

Fresh full native run: **293/293 passed, zero skipped**. The final focused server
run (population, current profiles and client/update admission) passed **73/73**.
The last workspace typecheck also passed. The history metadata optimization keeps
LP/title fields but omits its unused record-summary query; popup/profile consumers
retain record summaries. No compiler warning was disabled for the new changes.

### Main publication and initial Actions blocker (resolved below)

- Source main commit: `543dfec20684512a4f75adc3c462bcdd40059ba5`, normal
  expected-head push through the connected ybaf100 account.
- [Release run 40169454026](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454026),
  [Debug run 40169454013](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454013),
  and [server CI 40169454007](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454007)
  all stopped before any checkout/build step. Raw job metadata showed runner ID
  zero and an empty steps array. Two failed-job retries per run were accepted but
  again ended without steps. These are not compiler/test failures and neither
  native platform builds nor package/publish steps executed for beta.101.
- The connected GitHub Fetch tool does not support annotation URLs. A subsequent
  unauthenticated read of the public GitHub REST annotations endpoint succeeded
  and established the exact restriction on all four failed entry jobs:
  **"The job was not started because recent account payments have failed or your
  spending limit needs to be increased. Please check the 'Billing & plans'
  section in your settings"**. The other annotation is an unrelated Ubuntu image
  migration notice. This is an account billing/spending restriction, not a
  repository compiler/test failure or an unverified runner-capacity assumption.
  Release job `121103841227`, Debug jobs `121103842328` / `121103842424`, and
  server job `121103845834` confirm the same message on attempt three. Changing
  payment settings/spending limits is unavailable through the connected tools.
  The owner must resolve that account restriction in Billing & plans before
  these existing authorized workflows can execute; no fresh deployment approval
  is needed. No further blind retry or publication guard bypass is used.
- At 06:38:52 UTC both beta.101 R2 descriptors still returned HTTP 404.
  New immutable objects were not published and existing beta.100 objects were
  not overwritten. At that observation beta.101 R2 publication and production
  activation were incomplete; the resumed publication is recorded below.
- At 06:37:16 UTC Singapore health/ready returned HTTP 200, database/schema/public
  readiness true, and required client version plus both hashes/sizes still
  matched verified beta.100. Solo OFF and Survival 64-player ON were preserved.
  There is no beta.101 deployment attempt: its source marker explicitly prevents
  activation before verified client packages, even if ordinary auto-deploy is on.

Resume after the Actions execution restriction is resolved: retry failed jobs
in the source runs above (their push event retains `[publish-r2] [no-deploy]`),
check all platform/Debug OFF and publication jobs, download both complete
beta.101 packages and verify hashes/size/manifests/channel/native payloads, then
create a documentation-only `[deploy-lightsail] [build only]` main commit or
dispatch the existing Lightsail workflow in deploy mode. Verify the actual
Docker/upload/rollback activation and public API version separately. The standing
user deployment authorization remains in effect; do not ask for it again.

### Final isolated PostgreSQL 16 run

The final beta.101 source was tested again with a disposable local PostgreSQL 16
service at loopback, including the optional pool reconnection/idle-retirement
case. **All 70 server suites / 627 tests passed, zero skipped**; rules remained
**288/288**. This fresh run includes the history query omission, current version
admission, counters, active matching/recovery and existing achievements. The
expected local-backend termination log belongs to that disposable recovery test,
not Production Neon. Together with **293/293 native tests**, workspace typecheck,
server/rules build and package/localization checks, local validation is complete.
The initial Actions scheduling blocker and missing beta.101 publications were
separate from these passing local checks; their resolution is recorded below.

The final read-only Neon control-plane observation also showed the currently
deployed **beta.100** endpoint `idle`, last active `2026-10-25T06:37:15Z` and
automatically suspended at `2026-10-25T06:42:31Z` under its unchanged 300-second
policy. This is a real post-beta.100-deployment suspension observation; it is
not presented as a deployed beta.101 test. No SQL was used for that observation
and no manual endpoint suspend/setting change was performed.

### Resumed CI and verified beta.101 publication

After the owner resolved the GitHub account billing/spending restriction, the
existing source runs were retried without changing source or weakening any
release guard. **Attempt four of Release 40169454026, Debug 40169454013 and server
CI 40169454007 all succeeded**, on the same source commit
`543dfec20684512a4f75adc3c462bcdd40059ba5`.

- Release: Windows, iOS, Android32 and Android64 native builds, combined package
  validation and immutable R2 publication all succeeded.
- Debug: native regressions **293/293**, policy domains, software OpenGL ES shader
  rendering, all four platform Debug ON builds and Debug OFF compilation checks,
  combined package validation and R2 publication succeeded. Unchanged optional
  external images were skipped by the existing policy.
- Server: rules **288/288**, server **626 passed / one optional local pool test
  skipped**, PostgreSQL 16 online/concurrency/recovery **52/52**, workspace
  typecheck/build, relay and Apps Script checks passed. The adaptive simulation
  completed 1050 NORMAL matches and a ten-hour virtual soak. The corresponding
  local full run had already passed all **627/627**, including the optional
  pool reconnection/idle-retirement test.
- Both public `.geode` files were independently downloaded completely. Exact
  SHA-256/bytes matched their descriptors; embedded mod identity/version/channel
  and all four native binaries were verified. Existing immutable objects were
  not overwritten.

| Channel | Bytes | SHA-256 | Descriptor |
| --- | ---: | --- | --- |
| Release | 8,945,049 | `323d0378408f05e13ca9a41b302fb0e09b3d605cd7e3e0ca1d170f7fb7b2a745` | [release.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.101/release.json) |
| Debug | 9,525,435 | `4e465de5d8db45fe8c3864a3dc9b6b63c3b6f90dda6b5acbf5662cc3b856e79b` | [debug.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.101/debug.json) |

### Production baseline and activation

The read-only baseline at `2026-10-25T09:16:40.442Z` found the unchanged achievement
epoch `2026-10-24T04:59:47.102Z`; migrations 0020/0029/0030 applied; native_death
present; 47 accounts including bots, 52 Solo / 99 Survival matches and 150 / 23015
Attempts. There were 144 unlocks / 144 AP ledger rows and both AP totals were
5120; invalid LP ledger rows were zero. Survival remained ON with 64 players and
generation `survival-b0676719:64p`. These totals include legitimate gameplay
since the earlier beta.100 rollout and must not be restored to older snapshots.

Full Solo/Survival profile digests at this baseline were
`aeb6398d12f3ffe8cfa307d8d2cccba7` / `70a8a98aa85ead53129d442706d71816`.
No reset, destructive SQL, manual migration, Neon setting or manual suspension
was performed. This documentation-only `[deploy-lightsail] [build only]` push
activates the already tested beta.101 source after both public channels verified.
The existing Singapore host guard, pinned SSH known_hosts, StrictHostKeyChecking,
deployment concurrency and rollback path remain intact. Actual activation and
public API verification are appended after the workflow completes.
