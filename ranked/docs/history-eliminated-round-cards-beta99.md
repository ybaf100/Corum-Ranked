# beta.99 changes / beta.100 final publication: history, Survival records and Lightsail

Authoritative main base: `b65e5b5dcca3217bf6f6edf3e0e1229023f2cf28`.
Work branch: `fix/history-eliminated-round-beta99-20261025`.
Existing operator beta.98 commits and the subsequent history audio fix are
retained. The older unpushed beta.98 working area is preserved separately.

## History and Survival boards

Both list round chips and detailed round tabs use the confirmed own round rank:
gold first, bright silver second (`#F0F7FF`), bronze third, cyan fourth/fifth,
and green for other qualified ranks. Confirmed elimination, including forfeit,
is red. Missing own participation is gray. Unfinished rounds remain pending;
provisional standings are not treated as historical outcomes. `ownRank` is
returned only for a confirmed own result; legacy missing ranks are not guessed.
Selection changes the detail-tab text to cyan; unselected text stays white and
the border keeps its outcome color. The live second-place color is also brighter.

The result filter opens a settings-style modal with the valid options for the
current mode. Each option selects directly; dismissal, a replaced session,
a changed mode/page, and an already selected option do not trigger another read.
It closes before the session-expired reconnect modal is shown.

The compact waiting/live grid fits all 64 entrants in one screen. Card density
uses the actual living/qualified count: more than 30 shows icon/name only;
17–30 uses reduced cards without tier badges but with BEST and NOW progress;
16 or fewer uses full cards. A density change rebuilds the main board once.
Names shrink to fit. The standings popup uses the same thresholds when opened.
Native layout tests cover narrow, wide and tall viewports; device readability
and touch targets still require device checks.

The existing [history readability and safe map navigation](history-level-audio-beta99.md)
remain in place: unavailable thumbnail areas are omitted, map metadata remains
readable without the thumbnail provider, and exact recorded IDs open native
LevelInfoLayer without Ranked Ready/Attempts/results. A successful history scene
handoff stops owned active/crossfade/standby lobby BGM. Failed lookups retain
lobby audio; returning restores the current lobby music mode.

## Current public profiles and forum records

Authenticated history responses include a batched `currentPlayers` map for the
already authorized returned participants. The client displays the current mode
tier image with ambient effects, an explicit CURRENT LP/promotion value, and
the equipped scoped title with existing effects. These are current public data,
separate from historical tier snapshots. Missing values remain unavailable.

Solo, Survival and leaderboard player-info endpoints return a shared current
public profile: both Solo and Survival tier/LP, Solo promotion marks where
applicable, and the equipped title. Scaled information popups place the equipped
title below the name; the leaderboard list does so too. Titles are exposed only
when their exact achievement ID and level have an unlock row, preserving Hidden
secrecy. Scope remains part of identity; prefixes are not added twice.

Both Survival/leaderboard forum paths now use Singapore instead of the retired
Railway bridge. Exact account identity, match access, bot lookup exclusions,
cached GDKF/Pointercrate reads, bounded pending refresh, delegate ownership and
late-callback retirement remain in force. Public-profile reads neither create
accounts nor launch provider refreshes or background timers.

## Champion-only Survival wins and streaks

`readSurvivalRecords()` derives the record from settled, completed canonical
Survival matches within the existing history visibility policy. Only final rank
1 is a win. Every other positive final rank is a loss, even if it earned LP.
The streak counts consecutive championships in completion order, stopping at
the first loss. Active and pending-settlement matches are excluded.

Survival profile, leaderboard and public-profile responses share this definition.
The client renders `5전 2승 (40%)` / `5 MATCHES 2 WINS (40%)` and uses the existing
streak presentation. Solo W/L display and streak rules remain unchanged.
No reset or migration is needed. Match/Attempt history, LP/MMR, achievement epoch,
unlocks, AP ledger and previous achievement predicates are preserved.

## Updates and achievements

The update-required card has a bottom-left Release/Debug channel selector.
Release selection is immediate. Debug selection opens a masked password input
and must receive authorization from `POST /api/ranked/updates/channel/debug`.
The endpoint compares the existing configured Debug password in constant time
and uses an in-memory bounded attempt limiter. It needs no game session or DB
query, and does not return/log/store credentials. Missing Debug configuration
rejects selection. Cancelled, failed, detached and stale callbacks keep the old
channel. Switching is disabled during download, verification and restart.

Package version, hash, size, manifest, channel and atomic install guards remain
in force. The installed compiled channel remains authoritative for admission;
a UI selection never changes that identity.

One-shot achievements sort by public rarity from Common to Mythic, then AP
ascending within a rarity. Progressive slots retain their order and buttons
still reference original server card indices. Concealed cards use public fields
only. Existing earned IV/Legendary progress effects, V/Mythic effects and the
one-shot Unequip action remain covered by native regression tests.

## Hosting and rollout

Lightsail Singapore is the sole supported host. The Railway upgrade bridge and
Render deploy trigger are removed. The server refuses Railway/Render startup
before importing the game application or initializing Neon. No client fallback
to those platforms remains. Historical rollout documents remain archival.
The user reports Render is already stopped. Railway management requires renewed
authentication; external bridge shutdown has not been claimed as verified.

AGENTS.md records standing authorization to finish each patch with main push,
both immutable R2 channels, then Singapore activation. Pinned SSH/known_hosts,
StrictHostKeyChecking, Singapore checks, single writer, concurrency and rollback
are unchanged. Publication uses `[publish-r2] [no-deploy]`; activation uses
`[deploy-lightsail] [build only]` only after both public descriptors and full
package bytes verify.

Both beta.99 descriptors were HTTP 404 at preflight. This patch joins pending
beta.99; all six version metadata files already agree, with beta.98 as previous.
Production stays on beta.98 until package-first activation can finish. Earlier
publication runs failed before any runner/step executed. A billing/quota reason
is not asserted without evidence.

## Verification

Full native, rules/server, typecheck, build, localization and actual release
outcomes are recorded after execution below. Native harnesses execute production
C++ against pinned SDK JSON and Cocos/network/delegate boundaries. They do not
establish physical-device UI, FPS, audio playback or successful production Neon
suspension. Local idle SQL/reconnect checks are separate from compute suspension.

### Local execution and production preflight (2026-10-25 UTC)

- Full native suite: **291/291 passed**, zero skips, including actual filter and
  update-channel popup callbacks, public title/rank rendering helpers, density
  thresholds, rank palettes, history audio handoff and existing gameplay exits.
- Rules: **288/288 passed**. Server full run: **622 passed, one skipped, one
  stale mock assertion failed** because a human player-info request now performs
  one additional batched profile query. That expectation was corrected, preserving
  the unauthorized/bot query guard; the affected forum suite rerun passed **7/7**.
  All other server suites, including champion-only SQL, current profiles, Hidden
  title guarding, debug password HTTP validation and event-driven idle, passed.
- Disposable **PostgreSQL 16: 53/53 passed**, including all 52 online Survival
  integration tests plus the otherwise-skipped pool reconnect/30-second idle
  retirement test. It terminates only a local test backend and proves a committed
  receipt survives reconnect. It does not suspend a Neon production endpoint.
- Workspace typecheck and rules/server build: passed. Translation consistency:
  **679 UI keys / 48 achievements**, with native font coverage checks passed.
- Singapore `/health` and `/ready`: HTTP 200; database/schema/public readiness
  true, empty missing-schema list. Solo is intentionally OFF, so `rankedReady`
  false with `queueEnabled` false is not an outage. Survival is ON in 64-player
  format. Required version and both update channels remain beta.98.
- Read-only production preservation check at 02:57:51 UTC: migrations 0020/0029/
  0030 applied, native_death present, achievement epoch unchanged at
  2026-10-24T04:59:47.102Z; 22 unlocks/22 AP ledger rows, profile and ledger AP
  both 395; 52 Solo/94 Survival matches, 150/18009 Attempts; both complete profile
  digests match the prior handoff, and invalid LP ledger rows = 0. No production
  write, reset, migration, Neon setting change or manual suspension was made.
- R2 preflight at 02:57 UTC: both beta.99 descriptors HTTP 404; existing beta.98
  packages were not overwritten. Publication/activation outcomes follow below.

### CI follow-up and immutable version separation

Source commit `f127e20c7966c0c753382abdc42d79b3a324186e` was pushed normally
to main. The previous runner scheduling blocker is gone: both metadata jobs and
the four Release platform builds actually ran successfully. Debug native
validation found a GCC dangling-reference diagnostic in the promotion-mark
range expression. The code now copies the bounded mark list before iterating,
so a missing/malformed result array cannot leave a reference to a fallback
temporary. The actual helper test includes missing promotion results and passes
locally together with the Debug channel callback test (2/2).

The already running beta.99 Release build is separate from the corrected final
source. **Final publication/activation is beta.100**, using new immutable Release
and Debug object paths. The previous admitted version remains **beta.98**, since
beta.99 has not completed both channels. No existing beta.99 object is overwritten
and no required-version activation is performed until both beta.100 packages
are independently verified. All six version metadata files are synchronized.
