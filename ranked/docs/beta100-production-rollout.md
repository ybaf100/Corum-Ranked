# Corum-Ranked v0.5.0-beta.100 publication and Singapore rollout

## Source and preserved work

- Authoritative starting main: `b65e5b5dcca3217bf6f6edf3e0e1229023f2cf28`.
- Work branch: `fix/history-eliminated-round-beta99-20261025`.
- Feature commit: `f127e20c7966c0c753382abdc42d79b3a324186e`.
- Corrected final package source: `3010eb41417ac71738dcd121c716673e1ac580a4`.
- Both source commits were pushed normally with expected-main leases through the
  connected ybaf100 account. No reset or force push was used. The operator's
  beta.98 publication/activation/report commits and the history BGM fix remain
  ancestors. The older beta.98 staging worktree is preserved separately.
- The user's latest main-push/deploy instruction governs this patch; no new PR
  or additional deployment permission is required. The detailed implementation
  and earlier verification are in [the feature notes](history-eliminated-round-cards-beta99.md).

The final version is beta.100 to separate the corrected packages from an already
running beta.99 Release build. beta.99 Debug stopped at native validation before
publication; its promotion-mark lifetime diagnostic was fixed without disabling
warnings. Existing immutable beta.99 Release objects are not overwritten.
The previous admitted version is beta.98 until both beta.100 packages verify.

## Changes included

- Survival wins mean final rank 1 only; every other completed rank is a loss.
  Streaks count consecutive championships. Survival records show games/wins/rate;
  no historic record, LP/MMR or achievement reset is needed.
- Confirmed round placement colors in history list/detail tabs; elimination red,
  absence gray, brighter silver, selected text cyan, unselected text white.
- Direct result-filter popup, all 64 entrants visible in compact waiting cards,
  reduced BEST/NOW cards for 17–30 survivors, original cards at 16 or fewer.
- Current dual-mode tier images/ambient and LP/promotion in player information;
  scoped equipped title under names with existing effects, including leaderboard
  and history. History current LP is labeled separately from historical tiers.
- Lightsail forum paths restore cached GDKF/Pointercrate display. Existing exact
  identity, access checks, delegate ownership and callback retirement remain.
- Update-required Release/Debug selector; Debug requires server password
  authorization. No password or game-session bypass is embedded in the client.
- One-shot achievements sort by rarity then AP; progressive ordering and Hidden
  concealment remain. Earned IV/Legendary progress effects and one-shot Unequip
  behavior are preserved. The existing safe history LevelInfo/audio fix remains.
- Railway/Render runtime support and fallback URLs are retired. Historical
  rollout records remain archival; pinned Lightsail SSH and rollback are intact.

## Actual CI and package validation

- [Source CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40157130832): success.
  Rules 288/288, server 623 passed/one optional local-pool test skipped, PostgreSQL
  16 online integration 52/52, typecheck/build, Apps Script compatibility and
  1050 adaptive NORMAL games plus a ten-hour virtual soak passed.
- [Release](https://github.com/ybaf100/Corum-Ranked/actions/runs/40157130829): all four
  native platforms, combine/package and R2 publish succeeded.
- [Debug](https://github.com/ybaf100/Corum-Ranked/actions/runs/40157130836): native
  regression/policy, production shader rendering, all four Debug ON and OFF
  platform checks, package and R2 publish succeeded. Optional unchanged external
  images were skipped by their existing policy.
- Local native suite: 291/291. The corrected promotion helper/channel callback
  rerun passed 2/2; version/update tests passed 68/68 and package tests 11/11.
- Disposable local PostgreSQL 16: 53/53, including the otherwise optional pool
  reconnect/idle-retirement test. It terminates only a local disposable backend
  and verifies committed data survives reconnect; it does not stop Neon.
- Translation consistency: 679 UI keys / 48 achievements; font coverage passed.
- Both public packages were downloaded completely. SHA-256 and exact bytes match
  their descriptors; embedded mod ID/version/build channel and all four native
  binaries (Windows/iOS/Android32/Android64) were checked independently of CI.

| Channel | Bytes | SHA-256 | Descriptor |
| --- | ---: | --- | --- |
| release | 8,931,672 | `829fb54ac91e8fde05d9d9276a53c7680d318e30b866571743da946256124dee` | [release.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.100/release.json) |
| debug | 9,514,335 | `bc99d94790da77d0b6e098016c21c942ae367ec512ecf61503697be2327e4b11` | [debug.json](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.100/debug.json) |

## Production before activation

Singapore health and ready returned HTTP 200. Solo is intentionally OFF;
`rankedReady=false` with `queueEnabled=false` does not mean database readiness
failed. Database/schema/public readiness were true, missing schema was empty,
and Survival was ON with 64 entrants. Both current channels still required
beta.98 before this activation. No production setting was changed for testing.

The read-only preservation check at 02:57:51 UTC found migrations 0020/0029/0030
applied and native_death present; achievement epoch unchanged at
`2026-10-24T04:59:47.102Z`; 22 unlocks/22 AP ledger rows, both AP totals 395;
52 Solo/94 Survival matches, 150/18009 Attempts, invalid LP ledger rows zero.
Both complete profile digests matched the prior handoff. No DB reset, manual
ALTER, Neon configuration change, manual suspension or data deletion occurred.

The production Neon control API independently reported `current_state=idle`,
`last_active=2026-10-25T02:57:12Z`, `suspended_at=2026-10-25T03:03:16Z`, and the
existing 300-second timeout. Reading that control API does not issue SQL. This
is an actual pre-deploy suspension observation on beta.98, separate from the
local event-driven idle/pool tests; a post-deploy observation is recorded only
if actually made. New profile/history/stats reads run on explicit UI requests,
without new background DB polling.

The user reports Render is already stopped. Railway management requires app
reauthentication, so external Railway service shutdown is not claimed verified.
No Railway/Render deployment or old Seoul activation is performed.

## Activation and device limits

This documentation-only activation uses `[deploy-lightsail] [build only]` after
both immutable packages verified. The established workflow builds/uploads the
Docker image and invokes rollback-capable activation using pinned known_hosts,
StrictHostKeyChecking and the Singapore host guard. Actual workflow and public
health/config results are appended after execution.

Physical iPad/iPhone/Android/Windows rendering, touch, audio playback, FPS and
full real Geode game play remain device checks. CI/package/health success is
not represented as those device checks.

## Modified source paths

The following is the exact feature/version diff against the starting main;
D marks retired files. This rollout document is an additional documentation file.

```text
M	AGENTS.md
M	corum-ranked-mod/about.md
M	corum-ranked-mod/changelog.md
M	corum-ranked-mod/mod.json
M	corum-ranked-mod/src/RankedPopup.cpp
M	corum-ranked-mod/src/RankedRuntime.cpp
M	corum-ranked-mod/src/RankedRuntime.hpp
M	corum-ranked-mod/src/domain/AchievementPresentation.hpp
M	corum-ranked-mod/src/domain/HistoryPresentation.hpp
M	corum-ranked-mod/src/domain/PlayerInfoPresentation.hpp
M	corum-ranked-mod/src/domain/SurvivalBoardPresentation.hpp
M	corum-ranked-mod/src/domain/SurvivalHudPresentation.hpp
M	corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp
A	corum-ranked-mod/src/ui/PublicPlayerProfile.hpp
A	corum-ranked-mod/src/ui/RankedHistoryFilterPopup.hpp
M	corum-ranked-mod/src/ui/RankedLeaderboardPopup.cpp
M	corum-ranked-mod/src/ui/RankedModUpdateManager.cpp
M	corum-ranked-mod/src/ui/RankedModUpdateManager.hpp
M	corum-ranked-mod/src/ui/RankedPlayerInfoPopup.hpp
A	corum-ranked-mod/src/ui/RankedUpdateChannelPopup.hpp
M	corum-ranked-mod/src/ui/StandingVisuals.hpp
M	corum-ranked-mod/src/ui/SurvivalGameLayer.cpp
M	corum-ranked-mod/src/ui/SurvivalOutcomePanel.hpp
M	corum-ranked-mod/src/ui/SurvivalPlayerInfoPopup.hpp
M	corum-ranked-mod/tests/AchievementOverhaulNativeTest.mjs
A	corum-ranked-mod/tests/HistoryFilterNativeTest.mjs
M	corum-ranked-mod/tests/HistoryReadabilityNativeTest.mjs
M	corum-ranked-mod/tests/LeaderboardNativeTest.mjs
M	corum-ranked-mod/tests/ModUpdateNativeTest.mjs
M	corum-ranked-mod/tests/PlayerInfoPopupNativeTest.mjs
M	corum-ranked-mod/tests/PlayerInfoPresentationTest.mjs
A	corum-ranked-mod/tests/PublicProfileNativeTest.mjs
M	corum-ranked-mod/tests/RankedInputNativeTest.mjs
M	corum-ranked-mod/tests/RankedLobbyLifecycleNativeTest.mjs
M	corum-ranked-mod/tests/SurvivalBoardNativeTest.mjs
M	corum-ranked-mod/tests/SurvivalFreePlayNativeTest.mjs
M	corum-ranked-mod/tests/SurvivalPlayerInfoNativeTest.mjs
D	ranked/.render-deploy-trigger
M	ranked/README.md
M	ranked/apps/server/package.json
M	ranked/apps/server/src/achievement/achievement.service.ts
A	ranked/apps/server/src/achievement/public-title.ts
A	ranked/apps/server/src/config/debug-update-channel.controller.ts
A	ranked/apps/server/src/config/deployment-target.ts
M	ranked/apps/server/src/config/ranked-config.module.ts
M	ranked/apps/server/src/forum/leaderboard-player-info.controller.ts
M	ranked/apps/server/src/forum/match-player-info.controller.ts
A	ranked/apps/server/src/forum/public-player-profiles.ts
M	ranked/apps/server/src/history/history.service.ts
M	ranked/apps/server/src/history/survival-history.ts
M	ranked/apps/server/src/leaderboard/leaderboard.service.ts
D	ranked/apps/server/src/legacy/railway-upgrade-bridge.ts
M	ranked/apps/server/src/main.ts
M	ranked/apps/server/src/session/client-version.ts
M	ranked/apps/server/src/survival/survival-player-info.controller.ts
A	ranked/apps/server/src/survival/survival-records.ts
M	ranked/apps/server/src/survival/survival.service.ts
A	ranked/apps/server/test/debug-update-channel.test.ts
A	ranked/apps/server/test/deployment-target.test.ts
M	ranked/apps/server/test/forum-records.test.ts
M	ranked/apps/server/test/history-projection.test.ts
M	ranked/apps/server/test/history.test.ts
M	ranked/apps/server/test/leaderboard-player-info.test.ts
A	ranked/apps/server/test/public-player-profiles.test.ts
D	ranked/apps/server/test/railway-upgrade-bridge.test.ts
M	ranked/apps/server/test/standing.test.ts
A	ranked/apps/server/test/survival-records.test.ts
A	ranked/docs/history-eliminated-round-cards-beta99.md
M	ranked/package-lock.json
M	ranked/package.json
M	ranked/packages/rules/package.json
M	ranked/translations/ui.json
```
