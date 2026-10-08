# LevelInfo metadata preparation — historical implementation, 2026-09-07

**beta.33 update:** production Ranked/Death Match/Placement now acquire native
map/music/SFX and launch directly on the round screen. See
[BETA33-RELEASE.md](BETA33-RELEASE.md) for current ownership, READY, options and
return behavior. The LevelInfo-specific flow below remains relevant to the
separate Debug RECORD/TEST harness and historical investigations.

Base: `main` / `d116d74b0fdb755136ec226f326a53160a02eb00`, `v0.5.0-beta.14`.
No version bump, commit, push, server/rules change, or asset-pack activation.

## Status and evidence boundary

The reported Windows/Android failure (blank title, creator `-`, Stereo Madness,
missing actual-song download) has **not been reproduced on the affected device**.
The proven code defect was accepting saved objects or download payloads based on
ID/geometry alone. A completely empty GD cache already waited for native search;
"every fresh level opens before search completion" was not supported by source.

This change closes those unchecked paths and adds lifecycle diagnostics. It does
not claim that a native metadata parser, widget rebuild, or other mod has been
proven to be the originating cause. A native Geode build and device tests remain
required before treating this as a validated release fix.

Canonical requirements (latest 2026-09-06 sections):

- [Index](https://app.notion.com/p/3ce44344d8ba8114a916ea7f28eec697)
- [Client / UI / UX](https://app.notion.com/p/3ce44344d8ba81c18b4ef00ad1f2b94e)
- [Bugs / Regression / History](https://app.notion.com/p/3ce44344d8ba818d948dc4787b78670c)
- [Testing / CI / Release](https://app.notion.com/p/3ce44344d8ba811cad72e738f2800196)

## Implemented boundaries

1. Both Prepare and PLAYING reentry obtain their level from
   `RankedLevelPreparation`. An unattested saved object is never a scene fallback.
2. The resolver uses GD's exact online search and its parser/delegate. Only that
   exact query's list cache/timer is expired. Saved levels, geometry, statistics
   and user options are not purged. Each new competitive Map context performs an
   online lookup; attempts in the same verified context reuse it.
3. A successful, current callback attests a VALUE snapshot: ID, name, creator,
   user/account, primary song/track, version and online type. A retained pointer
   alone is not sufficient. Zero user/account IDs and built-in track 0 are valid;
   a literal creator name `-` is not automatically rejected. `songIDs` are logged
   but not required to equal the search snapshot: geometry may populate them later.
4. Scene init and native download/update completion revalidate the live object.
   A matching replacement is adopted so future entry uses the current full object,
   not a retained old list pointer. A mismatch blocks Play/Ready, invalidates that
   context's proof, and returns to Ranked for a fresh lookup after modals close.
   No server title/creator or synthetic song controls repair the native display.
5. Geometry readiness still includes native `shouldDownloadLevel()`. Song checks
   require proven metadata before the existing RobTop fast path. Optional song
   bypass cannot bypass metadata/geometry. Native download and Play remain owned
   by the real LevelInfo layer and its real Play button.
6. Metadata context includes match/run, playable level, round, DM sequence,
   Placement Stage and Map. PREPARE -> PLAYING does not change that identity.
   Native search callbacks have keys, not generations. Requests are serialized;
   a local timeout never starts an overlapping same-key retry. Context changes
   discard an immutable ticket, including observed A -> B -> A transitions.
7. The resolver delegate has process lifetime. It does not save a raw pointer to
   a previous scene delegate or steal an occupied delegate. The GD response hook
   runs native first and drains a pending ticket if its delegate was superseded.
   This fallback must be verified with native response tags on both platforms.
   Native HTTP remains responsible for terminal response/cancellation; an in-flight
   request is not fabricated as cancelled after a local timeout. A total metadata
   wait of 30 seconds reports through the existing map-failure route.
8. The original `settings-menu` / `settings-button` is checked for parent, listener
   and native callback identity. No replacement button or coordinate is created.
   Only that item is enabled; its menu's z-order is restored on teardown. Modal
   subtrees are not recursively locked and the Ranked cover is hidden over modals.
9. Auto Play checks for visible FLAlert-derived modals both before scheduling and
   immediately before actual button activation. The final boundary ticks authority
   and checks context, metadata, geometry, music, eligibility, busy state and modal.
   Arming occurs only at that boundary, not before waiting on a popup. Runtime's
   existing accepted-start preservation on actual native retries remains intact.
10. Native GameLevelOptionsLayer retains its input level. If native completion
    replaces that pointer while options are open, only the two native user values
    (`m_lowDetailModeToggled`, `m_disableShakeToggled`) are transferred to the same
    context's live level on close. No online metadata or scoring state is copied.
11. Jukebox's loaded-mod song widget exception remains. Both Ranked progress and
    LevelInfo use the same native song-resource check, with effective `pathForSong`
    file fallback on Android as well as Windows. Native resource/download checks
    and the real RobTop fast path remain. No song-name string classification.

## Diagnostic events

Filter Geode logs for `[RankedPrepare]`.

- `cache-before-lookup`, `lookup`, `online-result`, `lookup-finished`
- `scene-before-prepare`, `scene-before-reentry`, `init-before`, `init-after`
- `download-request`, `download-live-before`, `download-callback`, `download-live-after`
- `update-live-before`, `update-callback`, `update-live-after`
- `readiness`, `modal-hold`, `modal-resume`, `vanilla-options`, `play-activate`
- `*-metadata-invalid`, `*-stale-context`, `options-preserved-after-replacement`

Object events contain context, owner/level/widget pointer identities, ID, title,
creator/user/account, version, geometry length, downloaded flag, type, primary
song/track, secondary song IDs and widget custom ID/RobTop flag. Lookup events add
generation and key. Full levelString, credentials and HTTP bodies are not logged.
`metadataComplete` is structural evidence only; `readiness metadata=true` means
the live object also matches the accepted online proof. Readiness/modal logs emit
only on state changes, not every polling tick.

Locate the FIRST divergence between good/bad runs of the SAME playable level ID:

| First divergence | Investigate |
| --- | --- |
| Before scene | Cache/search source and exact callback metadata |
| Init input good, live object bad | Native init and other hooks |
| Init good, download/update bad | Native replacement, merging, parser completion |
| Live metadata good, widget bad | Native widget classification/rebuild, Jukebox hooks |
| Requested vs current context differs | Stale callbacks or obsolete scene |

If logs repeatedly show `init-after` or `download-live-after` losing metadata,
retain that trace. Do not weaken the proof, hardcode labels or remove the RobTop
path merely to make auto Play proceed. That requires native lifecycle investigation.

## Local automated checks

These three C++ programs compile independently of the Geode SDK:

```sh
c++ -std=c++23 -Wall -Wextra -Wpedantic -Werror tests/LevelPreparationTest.cpp -o /tmp/level-preparation-test
/tmp/level-preparation-test

c++ -std=c++23 -Wall -Wextra -Wpedantic -Werror tests/DomainTest.cpp src/domain/AttemptScoring.cpp src/domain/EnvironmentPolicy.cpp src/domain/HudPresentation.cpp src/domain/RenderFpsMeter.cpp src/domain/ServerClock.cpp -o /tmp/ranked-domain-test
/tmp/ranked-domain-test

c++ -std=c++23 -Wall -Wextra -Wpedantic -Werror tests/DebugBotDomainTest.cpp src/debug/DebugBotConfig.cpp -o /tmp/debug-bot-domain-test
/tmp/debug-bot-domain-test

node --test tests/LevelPreparationSourceTest.mjs
```

Run from `corum-ranked-mod`. All four checks passed locally using the portable
Zig 0.14.1 C++ compiler (Windows) and Node. The new policy tests cover metadata
proofs, stale/duplicate/discarded tickets and all 256 launch flag combinations.
Source invariants check wiring, not native ABI or rendered UI behavior. CI now
includes the new policy and source checks alongside existing regressions.

No Geode SDK/native mod build was available in this workspace. In particular,
native callback routing, selector identity and option/Jukebox behavior are NOT
claimed verified by the SDK-independent tests. Portable compiler/cache files are
under the ignored `build/analysis` directory; no system install/PATH modification.

## Required native/device verification

Build with manifest Geode 5.8.2 / GD 2.2081 on Windows, Android32 and Android64;
retain the existing iOS build as a portability regression gate. Do not publish an
official package until these native builds and the relevant device matrix pass.

| Axis | Cases |
| --- | --- |
| Level/cache | Fresh launch, fresh map, downloaded map, cached map, previously played map |
| Music | Custom not downloaded/downloaded, Jukebox absent/present, actual built-in song (including track 0) |
| Ranked flow | Round 1, Round 2, Round 3, Death Match, Placement |
| Asynchrony | Search/download/update delayed or failed, native delegate superseded, context changes with old response pending, same ID reused in a different context |
| Modal | Options/Jukebox opened before ready, held past countdown, opened between scheduling and activation, closed before/after eligibility expires |

Prioritize Windows and Android with a newly selected map whose geometry AND
custom song are missing, first without Jukebox. Record map ID, GD/Geode/mod
versions, platform, launch time, flow context, and good/bad logs. Then warm only
the map cache and repeat; then change only song/Jukebox state. Do not wipe a user's
whole GD save or song directory to create fixtures; use separate test profiles or
previously unused test maps. Repeat cold-first-entry tests and record failures / N.

Options acceptance: original button visible during Prepare; original popup's LDM
and Disable Shake work (use a map that supports LDM); download completion while
open does not lose either selection; no Play/arm occurs while open; close resumes
once if still eligible; elapsed server time is not restarted. Verify popup content
remains clickable and overlays never cover it. An unavailable selector logs a
warning and creates no substitute button. Inspect callback identity on that device.

Retest legal same-map reentry, fourth DM Attempt rejection, final-window expiry,
Practice ON/end-once/transport suppression/OFF-fresh, 3 Clear, Placement 2 Clear,
3/5 Stage and Top 2 persisted-grade isolation. LP/MMR, Bo3/tiebreaker, PERFECT WIN,
promotion/demotion, raw-percentage DM tie-to-new-map and opponent Finished/wrench
rules are unchanged. Asset-pack production values, FX/icon rendering, fixed server
URL, FMOD prewarm and transition tracing were not changed. Hitch remeasurement
remains a separate device task.

## Native references used (read-only)

- [Node IDs, pinned LevelInfo source](https://github.com/geode-sdk/NodeIDs/blob/fc9b3aa12a4eee67f0d26e3f115190cba2deddf1/src/LevelInfoLayer.cpp)
- [GD 2.2081 binding declarations](https://github.com/geode-sdk/bindings/blob/7f6c2a75742856de88dad354e576dcff8a28e881/bindings/2.2081/GeometryDash.bro)
- [Native options input retention (Windows)](https://github.com/geode-sdk/bindings/blob/7f6c2a75742856de88dad354e576dcff8a28e881/bindings/2.2081/inline/GameLevelOptionsLayer.cpp)
- [Geode 5.8.2 addresser](https://github.com/geode-sdk/geode/blob/v5.8.2/loader/include/Geode/utils/addresser.hpp)

GameLevelOptionsLayer -> GJOptionsLayer -> SetupTriggerPopup -> FLAlertLayer is
the native options inheritance chain. Native init/download/update parser bodies
are not supplied by the relevant binding declarations, so their metadata merge
semantics are measured by the added diagnostics rather than assumed from comments.
