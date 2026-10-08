# Ranked native Pause quit and Settings candidate (beta41)

This continues `work/ranked-pause-quit-order-beta41` from `ed73bd857bc0ebf3f4720f8243fd7fd7896d2ee2`, against production main `b418a75cd91aecbc6e787c09a8f6d5903870b874`. The prior candidate was reported unsuccessful. This is not a device-verified Pause fix; production main and the exact beta41 client/server gate remain unchanged.

## Latest device evidence and limits

`Geode 2026-10-08 01.10.48.log` records GD 2.2081, Geode 5.10.1 (loader `7e41336`, bindings `7f6c2a75`) and Corum beta41. Match `e5e2386a-5736-4238-8cde-83f91e55ee84` R1 (14975695) opens Pause at 01:13:26, 28 and 33. ROUND surrender is requested at 01:13:34, acknowledged with HTTP 201 at 01:13:35, and the lobby returns. R2 (77240630) starts at 01:13:55. Five requests through 01:14:00 reach the button/native pause entry, return unpaused, report valid UI/current-layer/button ownership and canPause, and increment the pause counter. No subsequent Ranked Pause opened message appears, while attempts continue.

The log establishes another failure after surrender, including a same-match R2. The earlier log already establishes a new-match R1 failure. Touch delivery and Round 2 alone do not explain it. The log does not include AppDelegate's paused value or the native pauseGame rejecting branch. All candidate packages use beta41, so version text alone cannot identify the installed commit. The user's failed result remains authoritative; missing new diagnostic lines do not invalidate it.

The Corum observer ordering defect in the previous record is reproducible, but correcting that defect did not establish device acceptance. Actual GD 2.2081 iOS native function bodies were not available. Binding addresses, older reconstructed GD source and host doubles are not disassembly or proof of this iOS failure's precise internal cause. There is no evidence attributing the failure to CBF, a particular mod, the server or polling.

## Pause change

Both concession observers now request one deferred handoff. The Pause watcher no longer removes/resumes Pause before a separate gameplay quit. The deferred handoff checks the exact current PlayLayer and its parent against the running scene; it does not reject a PlayLayer merely because native Pause suspended it.

An attached, exact-owned Ranked Pause with an authenticated concession for the same match, round and deathmatch enters the installed **PauseLayer::onQuit binding**. The existing Corum Pause hook permits only this concession, retires its own surrender modal, pause lease and watcher, and advances to next-original once. Ordinary/manual Ranked exit remains blocked. A strong Ref holds Pause through the native call and post-call cleanup.

If that native path reaches the existing PlayLayer quit hook, the hook does not re-enter Pause Resume while its native Quit caller is active. It permits gameplay quit only after native state is unpaused. Existing detached/refused Resume guards remain. Without an attached concession-owned Pause, the existing completion overlay and PlayLayer binding paths remain available. Native lifecycle details are deliberately left to GD; this routing is a candidate, not proof that the exact iOS rejecting guard is fixed.

Bounded read-only diagnostics add AppDelegate paused values before/after an unsuccessful native pause request and after the concession native quit. No private flag is written. No new native hooks, native priorities, PlayerObject hooks, pauseGame replacement, manually constructed PauseLayer, scoring/transport redesign or protocol change is introduced.

## Settings and volume

The Ranked root has a persistent bottom-right **Settings / 설정** entry, with the existing Corum surfaces, fonts and language preference. Its slider saves `ranked-volume` per user, from 0% to 100%, default 100%. New Korean glyphs are included in the existing generated font charsets. Changes apply immediately and the saved mod data is flushed on closing/exiting a changed modal.

The volume manager owns one FMOD master-output FADER DSP for the Ranked session. Linear output gain covers Corum lobby/result music, both channels of transitions, native Ranked gameplay music and SFX. This is required because Corum yields channel ownership to native gameplay. No GD saved volume, channel volume, fade points, playback position, download or stream is rewritten. A 0% setting gives exact silence. The personal multiplier composes with the user's existing GD volume choices.

The root's session lasts across its retained native gameplay children. Closing the root removes its own gain and restores ordinary GD output; session generations prevent an old root from ending a newer session. If output detachment fails, the gain is returned to unity for ordinary GD. Audio-engine replacement discards handles from the released System. Mount failures are bounded and retried with backoff. No global native audio detours are added.

Closing or leaving Settings unregisters only that Popup's force-priority registration once, independently of a delayed destructor. It has no scene or PlayLayer references. Root exit closes its own modal and cancels its settings menu touch. Music fade policy/configuration is unchanged at the user's request.

## Validation and acceptance

`RankedPauseConcessionNativeTest.mjs` compiles the actual Corum helpers/callbacks against explicit native-boundary doubles. It checks authenticated ownership, stale round/match/current layer, one binding entry/next-original call, lease/modal ordering, native refusal, next-match ownership and post-call object lifetime. These tests do not execute GD iOS internals.

`RankedVolumeNativeTest.mjs` compiles the actual manager against FMOD doubles and executes Settings exit cleanup. It covers both sides of fades, music/SFX scaling, zero gain, persistence, corrupted saved values, independent GD/external DSP volume, unchanged playback, stale sessions, System replacement, failure backoff, and delayed Popup destruction. Existing Pause, input, entry, spectator, scoring and audio regression suites remain required. Both new suites are included in automatic Debug CI.

The final push/build outcome is recorded by the branch commit and its automatic GitHub Actions run. CI and host tests establish source/compile/regression checks only. The precise native iOS rejection and successful device Pause after surrender are still unconfirmed; do not merge this candidate to production based on CI alone. No manual Release is requested or run.
