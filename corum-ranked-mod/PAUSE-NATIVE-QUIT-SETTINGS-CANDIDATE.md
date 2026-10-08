# Ranked native Pause quit and Settings candidate (beta41)

This continues `work/ranked-pause-quit-order-beta41` from `e7058efb6d2eee9a5d01d22bcc1f581d1c6ad47f`, against production main `b418a75cd91aecbc6e787c09a8f6d5903870b874`. The prior Pause candidates were reported unsuccessful. The asset download correction was accepted on device. This is not a device-verified Pause fix; production main and the exact beta41 client/server gate remain unchanged.

**Latest status:** the 12:25 device log and user accept automatic surrender return, but next-round Pause still fails. Quit returns with both pause states clear; a later boundary sets process pause again. The entry gate correction and the remaining native/device limits are recorded in [PAUSE-ENTRY-LIFECYCLE-CANDIDATE.md](PAUSE-ENTRY-LIFECYCLE-CANDIDATE.md). The evidence below describes the preceding return regression.

## Latest device evidence and limits

`Geode 2026-10-08 11.22.10.log` records GD 2.2081, Geode 5.10.1 and Corum beta41. Match `cf2dd8b3-8001-40c4-8034-fa3a28d7d910` R1 (56455318) opens Pause at 11:23:09, changing AppDelegate's paused flag from false to true. At 11:23:11, ROUND surrender receives HTTP 201, queues scene return, then logs **native quit blocked: scene still paused** and **native concession quit reachedPlay=false paused=true appPaused=true**. Automatic return is blocked by Corum's own guard while the native Pause Quit is active.

After a later return at 11:23:25, R2 (2997354) starts at 11:23:39. Five native pause requests through 11:23:42 have valid button/UI/current-layer ownership and local `paused=false`, but **appPausedBefore=true / appPausedAfter=true**. This establishes residual global pause state across rounds. The earlier logs also establish new-match R1 failure; touch delivery and Round 2 alone do not explain it. The new log directly identifies the Corum guard causing the new return regression, but does not retroactively prove every earlier failure had that same guard.

The old concession fixture assumed native Resume cleared both flags before entering PlayLayer Quit. The actual device log contradicts that assumption: the Corum boundary receives an intermediate paused scene. The fixture now exercises that ordering. Actual GD 2.2081 iOS native function bodies remain unavailable, so the precise internal pauseGame rejection branch is not established by binding addresses, older reconstructed GD source or host doubles. No evidence attributes the failure to CBF, another mod, the server or polling.

## Pause change

Both concession observers now request one deferred handoff. The Pause watcher no longer removes/resumes Pause before a separate gameplay quit. The deferred handoff checks the exact current PlayLayer and its parent against the running scene; it does not reject a PlayLayer merely because native Pause suspended it.

An attached, exact-owned Ranked Pause with an authenticated concession for the same match, round and deathmatch enters the installed **PauseLayer::onQuit binding**. The Corum hook retires its own surrender modal, pause lease and watcher, and explicitly calls that exact owner's **PlayLayer::resume binding** before advancing the native Pause Quit chain. Pause stays attached for its original Quit handler to close; the handler is never invoked on a window Corum has already removed. Strong Refs hold both objects across their native calls and post-call cleanup. Ordinary/manual Ranked exit remains blocked.

The exact authenticated native Pause Quit transaction may enter PlayLayer Quit with intermediate paused flags. Its permission no longer depends on those flags already being cleared, and it does not recursively re-enter Pause Resume. An internal Pause Resume retains ownership until its native Quit caller completes. Independent quit still requires both the local and AppDelegate pause states to be released by native Resume, including a detached Pause.

The concession helper reports success only when the native route reached the Corum PlayLayer quit boundary. Calling Pause Quit alone is no longer considered success. A refused route leaves the existing completion/PlayLayer fallback available. Current PlayLayer and running-scene ownership are rechecked after native callbacks, so a departed scene cannot quit its replacement. No private flag is written and no input fallback or retry loop is added.

Bounded read-only diagnostics record both pause states after concession Resume and native Quit, and before/after native pause requests. No new native hooks, native priorities, PlayerObject hooks, pauseGame replacement, manually constructed PauseLayer, scoring/transport redesign or protocol change is introduced.

## Jukebox button placement

The optional Jukebox button is now in the same row as LDM and Disable Shake in both round preparation and Placement map options. With Jukebox loaded, the three buttons share the row's Y coordinate; otherwise the existing two-button row is retained. The old song-row/side positions are removed. Its existing native callback, mod-loaded check, map attestation and modal cleanup are preserved.

## Settings and volume

The Ranked root has a persistent bottom-right **Settings / 설정** entry, with the existing Corum surfaces, fonts and language preference. Its slider saves `ranked-volume` per user, from 0% to 100%, default 100%. New Korean glyphs are included in the existing generated font charsets. Changes apply immediately and the saved mod data is flushed on closing/exiting a changed modal.

The volume manager owns one FMOD master-output FADER DSP for the Ranked session. Linear output gain covers Corum lobby/result music, both channels of transitions, native Ranked gameplay music and SFX. This is required because Corum yields channel ownership to native gameplay. No GD saved volume, channel volume, fade points, playback position, download or stream is rewritten. A 0% setting gives exact silence. The personal multiplier composes with the user's existing GD volume choices.

The root's session lasts across its retained native gameplay children. Closing the root removes its own gain and restores ordinary GD output; session generations prevent an old root from ending a newer session. If output detachment fails, the gain is returned to unity for ordinary GD. Audio-engine replacement discards handles from the released System. Mount failures are bounded and retried with backoff. No global native audio detours are added.

Closing or leaving Settings unregisters only that Popup's force-priority registration once, independently of a delayed destructor. It has no scene or PlayLayer references. Root exit closes its own modal and cancels its settings menu touch. Music fade policy/configuration is unchanged at the user's request.

## Validation and acceptance

`RankedPauseConcessionNativeTest.mjs` compiles the actual Corum helpers/callbacks against explicit native-boundary doubles. It checks exact authenticated ownership, explicit native Play Resume, attached native Pause Quit, intermediate paused entry, Resume inside native Quit, refusal/fallback, callback scene replacement, same-match R2/new-match R1 and object lifetime. The previous source fails the new executable at the Play quit assertion. Native flag release is simulated at native boundaries; these tests do not execute GD iOS internals or prove device acceptance.

`RankedVolumeNativeTest.mjs` compiles the actual manager against FMOD doubles and executes Settings exit cleanup. It covers both sides of fades, music/SFX scaling, zero gain, persistence, corrupted saved values, independent GD/external DSP volume, unchanged playback, stale sessions, System replacement, failure backoff, and delayed Popup destruction. Existing Pause, input, entry, spectator, scoring and audio regression suites remain required. Both new suites are included in automatic Debug CI.

The final push/build outcome is recorded by the branch commit and its automatic GitHub Actions run. CI and host tests establish source/compile/regression checks only. The precise native iOS rejection and successful device Pause after surrender are still unconfirmed; do not merge this candidate to production based on CI alone. No manual Release is requested or run.
