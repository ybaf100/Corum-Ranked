# Ranked Pause: entry lifecycle candidate after the 12:25 device failure

Base: connected **ybaf100/Corum-Ranked**, candidate `397a0f943394eccfe98161ee805f6c92333a3398`, production main `b418a75cd91aecbc6e787c09a8f6d5903870b874`. This remains a beta41 work-branch candidate. Production main, the exact client/server version gate and protocol v2 are unchanged.

## What the device establishes

`Geode 2026-10-08 12.25.15.log` records GD 2.2081 / Geode 5.10.1. In match `f53c8e8c-123e-4bcc-86ef-584dd11a151f`, R1 DeCode (2997354) opens Pause at 12:25:48. ROUND surrender receives HTTP 201 at 12:25:50. Both concession Resume and native Pause Quit return with **local paused=false / appPaused=false**; native Quit reached Play, and the Ranked child returns. The user accepts automatic return.

R2 Error (56455318) initializes at 12:26:07 and releases at GO at 12:26:11. All five subsequent Pause requests have valid button/UI/scene ownership and **local paused=false / appPaused=true**, without a Ranked Pause opened record. Earlier evidence includes the same failure in a new match R1. The GO message in that binary did not measure AppDelegate pause state.

The previous assertion that native Quit never restored the global state is contradicted by this log. The state changes again after the native Quit return and before the next Pause request. The actual deferred `popScene` and native `onExit` happen after the Quit-return message. That message does not certify completed retirement or entry into the next gameplay scene.

## Confirmed Corum defect and limits

Corum's entry hold sets the local `PlayLayer::m_isPaused` bit. Its old certified-GO path restores that bit alone, opens input, flushes the deferred native attempt count and resumes gameplay music, without checking GD's process pause state. The actual callback passes a scene with local=false / process=true. This is an executable source defect that matches the state mismatch observed on the device.

The previous concession fixture ended at a native Quit double that cleared both flags. Its next-round/new-match checks omitted the later queued return and entry gate. They therefore did **not** establish that real GD retirement and re-entry preserve that clearance. The extended entry fixture now compiles the actual Corum deferred-return, init-hold and GO callbacks together and injects a process-pause change at explicit native boundaries. It covers changes during the deferred pop and during the following entry, in R2 and new-match R1. The old GO callback fails the new executable at the next-entry state assertion; the candidate passes.

**Not established:** the exact GD 2.2081 iOS instruction/function that writes the flag in this interval, the precise native `pauseGame` rejection branch, or successful device acceptance of this candidate. No iOS GeometryJump executable/disassembly is available. Boundary doubles are not native GD execution. There is no evidence attributing the failure to a particular mod, CBF, the server or polling.

## Geode and lifecycle references

Reviewed the official Geode [modify rules](https://github.com/geode-sdk/docs/blob/7894c810014a5a89fca0da8f78e54801c1931b2c/tutorials/modify.md), [memory rules](https://github.com/geode-sdk/docs/blob/7894c810014a5a89fca0da8f78e54801c1931b2c/tutorials/memory.md), [Fields](https://github.com/geode-sdk/docs/blob/7894c810014a5a89fca0da8f78e54801c1931b2c/tutorials/fields.md), and [manual hooks](https://github.com/geode-sdk/docs/blob/7894c810014a5a89fca0da8f78e54801c1931b2c/tutorials/manualhooks.md). Checked the actual Geode 5.10.1 [`Ref`/`WeakRef` implementation](https://github.com/geode-sdk/geode/blob/7e41336/loader/include/Geode/utils/cocos.hpp) and native binding/codegen at `geode-sdk/bindings@7f6c2a75742856de88dad354e576dcff8a28e881`.

Callbacks outside a hook enter through the native binding/object interface. Qualified next-original calls remain inside their hooks. Strong `Ref` ownership must cover native callbacks and post-call fields access; a removed child cannot be assumed alive because a raw pointer still exists.

The [Cocos 2.2.3 scene director](https://github.com/cocos2d/cocos2d-x/blob/cocos2d-x-2.2.3/cocos2dx/CCDirector.cpp) calls transition-completion callbacks for ordinary scene pushes, too. Missing a Fade transition alone is not evidence of a missing callback. [CCLayer](https://github.com/cocos2d/cocos2d-x/blob/cocos2d-x-2.2.3/cocos2dx/layers_scenes_transitions_nodes/CCLayer.cpp) registers input during `onEnter`; replaying Resume/onEnter on an already-running layer can duplicate registrations. Older reconstructed [GD PlayLayer source](https://github.com/aloaf812/GD/blob/de2da70f9da63a268d87f818617708736fbc894f/Classes/PlayLayer.cpp) shows native entry completion clearing process pause and Resume re-entering the layer. These sources inform API selection, but are **not proof of current GD iOS bodies**.

## Candidate behavior

At the existing server-certified GO boundary, Corum requires the exact current, running PlayLayer and an attached running parent. If the originally unpaused Ranked entry still has process pause set, it invokes **that PlayLayer's native `onEnterTransitionDidFinish`** through the object interface, once in this release transaction. It does not replay Resume/onEnter or alter the process flag directly. An in-progress latch prevents a nested callback from releasing GO twice, and a strong Ref keeps the exact layer alive through the call and subsequent checks.

After the callback, Corum rechecks current scene, attachment, match, round, deathmatch, level, server state, both entries, countdown and start certificate. Both local and process pause must be clear before input, the deferred attempt count, overlay release or gameplay audio are released. A native refusal or changed scene/context keeps the hold and schedules one exact-owner native Quit outside the lifecycle/update stack. A departed scene cannot quit or pause audio in its replacement. There is no retry loop.

An already clean R1 needs no extra completion call. A genuinely paused pre-entry scene keeps its original paused preference. Ordinary GD and Placement do not acquire this Ranked entry gate. Authenticated surrender, its accepted native Pause Quit route, spectator/scoring safety and native transport gates remain intact. No new native hooks, priorities, PlayerObject hooks, Pause fallback or manually constructed Pause window are added.

Bounded read-only messages now include process pause at init hold, before/after the existing native exit hook, after conditional native completion and at GO. These distinguish completed retirement, entry and release in the single final candidate; there is no separate diagnostic installation series.

## Validation

The executable fixture covers deferred retirement and R2/new-match R1, clean R1, native refusal, changed scene/context/start certificate, re-entry, strong lifetime through native removal, one-shot release, unchanged input registration, original paused preferences and ordinary GD. It compiles under both Windows and iOS test macros with strict warnings; neither is an iOS device execution.

Local validation passed: all **112 native/source tests**, **nine strict C++ executable suites**, **five asset-pack build tests**, and the actual Debug workflow's manifest/client-server version validation (beta41). The extended entry test also passes independently under both test platform macros. The previous callback fails the new next-entry assertion. Automatic platform CI remains a separate build check; its outcome is reported with the final work-branch commit. CI cannot substitute for device acceptance. Do not merge this candidate solely because checks pass.

Assets, Settings/volume, difficulty icons and optional Jukebox placement are preserved. Music fade policy remains unchanged at the user's request. The asset ZIP is packaged only when its source changes; this code-only patch does not require an asset-pack upload. No manual Release is executed.
