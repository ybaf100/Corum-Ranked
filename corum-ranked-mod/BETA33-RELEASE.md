# v0.5.0-beta.33 — prepare and launch on the round screen

Starting main: `6137b7e44ea166c29c02e404268bdb3e975ceec5` (beta.32).

Ranked / Death Match / Placement preparation now downloads the canonical map,
music and GD 2.2 song/SFX dependencies automatically on the existing Ranked
screen. The existing MAP DOWNLOAD / MUSIC DOWNLOAD rows are also bounded retry
controls. They no longer open LevelInfo. MAP READY / AUDIO READY and native
file counts show completion; the map endpoint supplies no byte percentage, so
the preparation bar is file-weighted, not an invented map download percentage.

LDM and DISABLE SHAKE buttons update only the native Saved level's
`m_lowDetailModeToggled` / `m_disableShakeToggled` preferences. The author's
capability flags, metadata, geometry and local scores are untouched. The buttons
become available after verified native geometry acquisition. The same Saved
object, including the selected options, enters native gameplay.

## Native preparation and lifecycle

- Retain the existing exact online metadata proof before accepting saved geometry.
  Use GD `GameLevelManager::downloadLevel(id, false, 0)`; never synthesize a level
  or copy an encoded level response into an older list object. A metadata/version
  mismatch invalidates the proof and requests fresh online attestation.
- A process-lifetime native delegate owns callbacks, independently of UI scenes.
  Foreign map delegates/active keys are respected. An obsolete untagged map
  request drains before reuse, including A → B → A context changes. Callback
  bodies only latch immutable results; retries/navigation happen outside callbacks.
- Fetch NG metadata with its real song URL before `downloadSong`. Load native
  library/content URL when required; download every song/SFX dependency with at
  most three occupied native resource slots. A callback or downloaded cache flag
  alone does not establish readiness: check GD's effective, nonempty file path,
  including Jukebox/NONG replacements. Native packaged resources remain usable.
- Requests retry at bounded 2/4/6-second backoff with three attempts, then expose
  an in-screen retry. Never cancel/reissue still-active foreign or timed-out native
  requests. The process heartbeat drains callbacks even while the screen is covered.
- Preserve authoritative READY timing (Ranked ten seconds / Placement five),
  using the server deadline. Retry READY at one-second cadence until ACK.
  Only server-authorized playable states can enter `PlayLayer::scene`.
- Real gameplay arms at the final launch boundary and retains existing journal /
  Start ACK behavior. Read-only spectator entry never arms an Attempt. The beta.30
  timer-zero active final Attempt and beta.32 surrender/pause rules are unchanged.
- Native gameplay is pushed over the retained round scene. The existing
  `PlayLayer::onQuit` wraps native teardown in an exact scene-owned return scope.
  One cold `GameManager::returnToLastScene` hook consumes only that scoped return
  and queues `popScene` after the old quit stack finishes. Ordinary gameplay and
  the Debug RECORD/TEST LevelInfo path retain vanilla return behavior. No additional
  PlayerObject or per-frame world hooks are registered; `setVisible` stays absent.

The old LevelInfo gate remains for the separate Debug RECORD/TEST harness and
legacy recovery callers; production round-screen entry does not invoke it.

## References inspected, no wholesale source copy

- [Versus MapCache](https://github.com/Rlaxogus123/Versus/blob/69cfb4f2e7b39f426797a0a3d7d4389f63dac755/src/MapCache.cpp)
  and [MatchLaunch](https://github.com/Rlaxogus123/Versus/blob/69cfb4f2e7b39f426797a0a3d7d4389f63dac755/src/MatchLaunch.cpp):
  waiting-screen native acquisition, bounded song/SFX preparation, native scene
  entry and cold GameManager return boundary. Versus's failure callback treats
  its argument as a level ID in one path; Corum uses the verified GD response/error
  signature and does not copy that assumption.
- [GMDEndless NativeRequests](https://github.com/ybaf100/GMDEndless/blob/cf2ad3586f354117e43b5192e07c4793d385aea0/src/geode/NativeRequests.cpp),
  ResourcePreparation and MapService on `fix/m2h-native-map-load`: persistent
  ownership, immutable generations, native key drain, resource metadata and
  construction at a prepared gameplay boundary.
- GD 2.2081 bindings: native map/song/SFX methods, Saved level options,
  `PlayLayer::scene` and `GameManager::returnToLastScene` exist on target platforms.

## Verification and device boundary

The actual complete round adapter and option handlers execute in a strict C++
native double: map delegate contention; same-tick pump deduplication; song-info /
content URL / multi-song / SFX sequencing; callback/cache without a real file;
bounded failure and explicit retry; A → B → A delayed callbacks; changed level
version; modal/server launch gates; exact Saved object/options; spectator no-arm;
duplicate return suppression; queued round return; ordinary non-Ranked inactivity.
Existing Placement, persistence, Spider segments, world-driver, camera, Safe Mode,
Debug RECORD/TEST and package hook-inventory regressions remain required.

Client/workspace/lock/server exact gate are beta.33. Spectator protocol v2 and
Debug recording format v2 are unchanged. No server rule/protocol or DB migration.

**Not device verified.** Native compilation and headless tests do not establish
real iPad download/audio/navigation correctness or stability. Device acceptance:
fresh uncached map + missing song, previously cached map, built-in track 0,
multi-song/SFX, optional Jukebox, LDM/shake choices, all rounds + same-map DM
re-entry, Placement final Attempt Clear at timer zero, spectator return and
ordinary Practice with the same enabled mod stack.
