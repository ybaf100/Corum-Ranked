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
