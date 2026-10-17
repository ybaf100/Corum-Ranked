# Survival Pause and board recovery — beta.75

Base: `b8f21c98c4e5afe720bb077d1edd95e8d0be9ff4` (main, beta.74).
Comparison: beta.72 (`43795dcd4dba618114e0ec28c1707ea8f43e4dbd`).

## Findings and changes

- The beta.73 touch adjustment was in a new `PauseLayer::onEnter()` method. Geode's generated PauseLayer modifier has no onEnter hook, so native Pause creation never dispatched that method. Synchronize registered menu priorities from the existing scheduled pause watcher instead, in Debug ON and OFF. Keep Survival surrender outside the native icon row and leave Solo's native X/lease policy intact.
- Force Clear was allocated only when `canForceClear()` was already true during customSetup. A delayed preparation/entry acknowledgement could permanently omit it from that pause. Allocate once for the authorized Debug/offline context and retry setup during the watcher. Actual activation and submission still require the original canForceClear checks, pending-request guard and server ACK; online/release/Death Match paths remain excluded.
- Surrender incorrectly depended on gameplay's remaining-time/attempt eligibility just to open confirmation. Use separate session/state surrender admission; missing countdown samples cannot suppress surrender. Retain the serialized Attempt End / FORFEIT / authoritative elimination / deferred native quit path. Clear detached modal handles so a removed confirmation cannot permanently disable the action.
- Use Popup's built-in button menu with its native layout and modal priority. Preserve deferred confirmation callbacks and exact scene/context checks. Popup owns its force-priority cleanup; close/exit cannot invoke an additional manual release from this confirmation.
- Result headings previously always reserved the right clock column, even when displaying a full-width result. Center the result/elimination heading across the available width, hide the clock during elimination, and fit headings vertically. Give card names, avatars and record text separate measured rows. Result totals outrank transient polling errors.

## Crash evidence and limits

The attached iOS report identifies `AudioEffectsLayer::init` / `CCTextureCache::addImage` during native PlayLayer creation, not a surrender callback. It does not prove the original memory corruption source. Reacquire/retain the *current* texture-cache singleton for each native launch; holding the first cache does not protect a replacement after texture reload. Reject launch while a next scene is pending or any native PlayLayer owner remains, including a non-running owner still being torn down. Test both cases and singleton replacement with production launch code.

These checks repair concrete entry ownership hazards. The supplied iPadOS/Geode 5.11.2 JIT-less crash cannot be declared resolved without device reproduction. Recheck GD levels 95078494 and 148827436, cold entry, repeated exits/re-entry, app background/resume, and surrender while paused. No engine-wide texture hook, suppressed native AudioEffectsLayer, levelComplete simulation or vanilla reward mutation is introduced.

## Delivery

Keep the existing Validate Corum Ranked, Build Corum Ranked Debug and Build Corum Ranked Mod workflows. No new workflow. Push beta.75 to main without waiting for workflow results. Server client-version changes trigger the existing Render root-directory deployment; a build-only push retains the existing R2 publication exclusion.

## Local verification

- Full Native harness suite: 202/202 passed, including actual pause callbacks, modal choices, End/FORFEIT ACK handling, exact retries, Free Play, scene ownership, cache replacement, dense card bounds and Solo pause regressions.
- Server Survival Debug, Survival online and client-version suites: 120/120 passed (PGlite-backed integration; no production database writes).
- Android64 Debug ON and Android32 Debug OFF compile/link passed against Geode 5.8.2 / NDK r29. ELF architecture and Force Clear string inclusion/exclusion checked. These are shared-library builds with Geode CLI packaging disabled, not device runs.
- Android64 Debug OFF compiled/linked earlier in this change; final OFF source was additionally compiled as Android32. Windows/iOS beta.75 builds and all device behavior remain unverified locally. Existing GitHub workflows will perform platform packaging after the main push; their results are not awaited.
- No R2 upload, workflow additions, unrelated repository changes, new GD engine-wide hooks or LP/MMR algorithm changes.
