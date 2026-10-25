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

### Main publication and external Actions blocker

- Source main commit: `543dfec20684512a4f75adc3c462bcdd40059ba5`, normal
  expected-head push through the connected ybaf100 account.
- [Release run 40169454026](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454026),
  [Debug run 40169454013](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454013),
  and [server CI 40169454007](https://github.com/ybaf100/Corum-Ranked/actions/runs/40169454007)
  all stopped before any checkout/build step. Raw job metadata showed runner ID
  zero and an empty steps array. One failed-job retry per run was accepted but
  again ended without steps. These are not compiler/test failures and neither
  native platform builds nor package/publish steps executed for beta.101.
- Check runs advertise two annotations each, but the connected GitHub Fetch
  endpoint rejects the annotations URL as unsupported. No billing/quota/account
  cause is asserted without the actual annotation text. No further blind retries
  or release guard bypass is used; the owner was asked for that text.
- At 06:38:52 UTC both beta.101 R2 descriptors still returned HTTP 404.
  New immutable objects were not published and existing beta.100 objects were
  not overwritten. **beta.101 R2 publication and production activation remain
  incomplete**, so these new UI changes are not yet available in a public client.
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
The Actions scheduling blocker and missing beta.101 publications remain separate
from these passing local checks.

The final read-only Neon control-plane observation also showed the currently
deployed **beta.100** endpoint `idle`, last active `2026-10-25T06:37:15Z` and
automatically suspended at `2026-10-25T06:42:31Z` under its unchanged 300-second
policy. This is a real post-beta.100-deployment suspension observation; it is
not presented as a deployed beta.101 test. No SQL was used for that observation
and no manual endpoint suspend/setting change was performed.
