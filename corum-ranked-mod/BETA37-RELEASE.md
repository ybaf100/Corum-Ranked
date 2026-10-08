# v0.5.0-beta.37 — Clear persistence boundary and entry Pause

Starting connected GitHub main: `c3c5af560b5b4dbf4322ca01952e59973a58a8a7`
(beta36). Includes the higher-resolution Paperlogy UI/countdown fonts from
beta36. Manifest, workspace/lock and exact server client gate use beta37;
Spectator protocol 2 and Debug recording format 2 are unchanged.

## Clear evidence and scope

The attached `2026-10-07_05-09-15.989910.log` is a SIGSEGV in libc++ string
copy, with source-register candidate `x1=0x3ff800`, fault `0x3ff817` and LR
`0x1084b104c`. The corresponding session installs Corum's global
`GJGameLevel::storeNewLocalScore` at `0x1084b0f54`: LR is target +0xf8.
The caller is native `PlayLayer::levelComplete +0x550`. The user reports
Clear crashes in every mode. This is different from the earlier setVisible
SIGABRT/Tulip registry-growth crash.

The official GD 2.2081 released bindings declare the int/int score boundary
at iOS `0xacf54`. That declaration does not establish the safety of the actual
iOS detour/trampoline. The original game binary and a new device comparison
are unavailable here: the precise source of the invalid string pointer remains
unproven. This patch eliminates the implicated Corum score detour; it does not
patch private Tulip code, clamp pointers, swallow exceptions, or blame another
mod in the completion chain.

`storeNewLocalScore` is no longer registered on any platform, and the earlier
`saveNewScore` detour remains absent. Genuine local, Ranked, Placement and
Debug RECORD Clear reach the vanilla score path without either Corum detour.
The accepted final Placement Attempt still continues at timer=0; no scoring or
timer rules are changed.

Spectator suppression remains at exact scene/death/completion/end/New Best/
attempt-count boundaries and exact level-object `savePercentage`. Native
reset (including terminal handoff), quit and exit cleanup additionally use a
scoped `m_isTestMode` no-save flag only when the exact scene has no persistence
authority. The flag is restored on scope exit/nesting; ordinary scenes are
not written by the scope at all. This is an additional cleanup backstop,
not the basis for spectator result/transport authority. No low-level score
writer is called by Corum spectator presentation. Arbitrary direct calls from
other mods to GD's score API are not intercepted by a Corum global score hook.

## Pause

The former GO path restored the button's transient init-time disabled value.
The executable ordering regression reproduces init disabled -> native scene
entry enabled -> old GO disabled. During entry we now hold the native menu
lock; at a successful server-certified GO, an unpaused scene releases it using
`UILayer::enableMenu()`. Previously paused scenes retain their state. Input,
queued-button cleanup, deferred attempts and audio resume ordering remain.
Two bounded logs show pre-hold and GO pause states; no per-frame logging or
new input/PlayerObject detour is added.

## Verification boundary

The native callback doubles exercise real production bodies for genuine Clear,
Ranked/Placement saves, Debug RECORD/TEST, spectator completion/cleanup,
restored/nested no-save scopes and late entry-menu enable. CI checks actual
packaged detour markers: both score/string hooks must be absent; percentage
and all relevant scene safety boundaries must be present. Existing camera,
Spider, World Driver, transport, pause/surrender and font suites are retained.
Local validation passed: 89 source/native callback tests, 9 strict C++ suites,
114 rules and 186 server tests, workspace typecheck and build. The final added
spectator-reset cleanup callback and scope restoration checks also passed.

Host doubles and CI compilation cannot execute the original GD iOS score
implementation or the iPad touch dispatcher. Actual all-modes Clear, opening
Pause after GO, atlas rendering and spectator no-save validation on the same
iPad/mod stack remain required. No device-fixed/device-verified claim or
manual Release workflow is made.
