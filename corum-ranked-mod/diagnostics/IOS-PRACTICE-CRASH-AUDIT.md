# Ordinary Practice SIGABRT: evidence and hook audit

**Root cause is not yet isolated. These are beta.30 diagnostic packages, not a beta.31 fix.**
Authoritative main is `58d643d21734243e2f3a45e3006d27f178cad83e` / `v0.5.0-beta.30`.

## Device evidence and outstanding report comparison

The user reports a fixed iPad/GD/map/other-mod set: Corum ON reproduces the crash,
only Corum OFF does not. This implicates Corum as a necessary condition or strong
trigger, without proving a particular Corum hook body corrupts memory. A Geode frame
does not exonerate Corum. Other mods have not been identified as the cause.

Both report originals are missing from the conversation attachments/workspace. This
table deliberately distinguishes user-provided signature from independently read data.

| Item | `07-56-18.626275.log` | `05-01-27.095985(1).log` |
|---|---|---|
| Signal | User: SIGABRT; original pending | User: same/similar; original pending |
| Fault thread | Unknown | Unknown |
| Top native frames | User: libc++ / allocation / `operator new[]` | User: same/similar |
| Tulip frames | User: vector `__append` → `Pool::getCommonHandler` | User: same/similar |
| GeometryJump address/offset | Missing | Missing |
| Animation frame | User: `SpriteAnimationManager::callAnimationFinished`; offset missing | User: same/similar; offset missing |
| Registers/saved stack | Missing | Missing |
| Corum version | User references beta.30/beta.29; not independently verified | Not independently verified |
| Geode version | User specifies 5.10.1; not independently verified | Not independently verified |
| Mod set | Exact list missing; user mentions Globed/Eclipse/CBF/QoLMod/CR AC/GMDEndless | Exact list missing |
| Scene/level | User: ordinary Practice without Ranked/Placement/Spectator | Exact metadata pending |

The removed Placement `saveNewScore` hook caused a different reported SIGSEGV path.
Its beta.30 removal and safer exact-level store guard remain intact; they do not prove
resolution of this SIGABRT.

## Exact Geode/Tulip and ARM64 findings

Connected GitHub reads pinned:

- [Geode 5.10.1 commit](https://github.com/geode-sdk/geode/tree/7e41336f68660b990644f5c3450b6da6ae944560)
- [TulipHook 3.1.14 commit](https://github.com/geode-sdk/TulipHook/tree/a448204611002990eb83c323f580cadc310e9cae)
- [Pool.cpp](https://github.com/geode-sdk/TulipHook/blob/a448204611002990eb83c323f580cadc310e9cae/src/Pool.cpp#L48)
- [ARM64 common handler](https://github.com/geode-sdk/TulipHook/blob/a448204611002990eb83c323f580cadc310e9cae/src/generator/ArmV8Generator.cpp#L444)
- [Separate nesting stacks](https://github.com/geode-sdk/TulipHook/blob/a448204611002990eb83c323f580cadc310e9cae/src/Handler.cpp#L295)

Geode 5.10.1 pins Tulip 3.1.14. Pool's thread-local vector takes the missing/NULL
registry-slot path: resize(uniqueIndex+1) → lookup/create handler under mutex → add
original trampoline → increment separate nesting index → select detour. uniqueIndex
is a function-registry index, not the nesting-stack index. Even a lower NULL slot can
resize the registry vector downward and lose higher cached slots; this is
an upstream source observation, not proof of the reported allocation failure.

The official Geode 5.10.1 iOS dylib was downloaded outside the repository, parsed as
Mach-O and disassembled with Capstone. No Geode source/library was vendored or patched.

- UUID: `951c0bd1-b855-3f88-8942-c2a408cab5ab`
- SHA-256: `74bf8d2dc1e80f843c1af36e4daf0eb1c3bf496cdebe652f94b8f716b7bfa26b`

These are **released-image-relative offsets**, requiring matching device UUID/build
and subtraction of its ASLR image base before a crash instruction can be identified.

| Offset | Disassembled operation |
|---|---|
| `0x7f1ba0` | Pool member getCommonHandler entry |
| `0x7f1bbc` | cmp w5,1: exit path decrements nesting, does not resize |
| `0x7f1bd0` | mov x20,x2: preserve incoming uniqueIndex |
| `0x7f1c14` | Compare index with TLS vector size |
| `0x7f1c24` | Requested size = index+1 |
| `0x7f1c40` | Extra element count for vector growth |
| `0x7f1c50` | bl 0x7f2144: vector __append |
| `0x7f1f18` | Static wrapper shifts arguments to make room for Pool* this |
| `0x7f2144` | __append saves caller X20/X19 at its SP+0x30 |
| `0x7f2170` | __append reuses X20 as old buffer pointer |
| `0x7f21bc` | lsl x0,x23,3: allocation bytes = capacity*8 |
| `0x7f21c0` | Call import stub 0x87f00c, resolved to __Znwm (operator new(size_t)) |

This independently verifies the resize→vector-growth→allocation path in the released
binary. It does not prove either missing report matches those offsets or its
`operator new[]` wording matches this import. Compare exact reports/build UUID first.

At **static wrapper entry**: X0=original, X1=uniqueIndex, X2=offset, X3=commonHandler,
W4=handlerType. At **member Pool entry**: X0=Pool*, X1=original, X2=uniqueIndex,
X3=offset, X4=commonHandler, W5=handlerType. Disassembly verifies the shift.
Pool preserves index in X20, but __append saves and then reuses it. Abort registers
X1/X2/X20 are therefore not automatically Pool arguments. Recovering them requires
appropriate saved-frame/stack data; final signal registers alone may be insufficient.
No device uniqueIndex value has been recovered yet.

**OOM, heap/vector corruption, corrupted uniqueIndex/generated state, nesting imbalance,
fixed common-handler space and hook-count/mod interaction remain unproven hypotheses.**
No private Tulip patch, guessed index clamp, allocation workaround or exception swallowing
was introduced.

## Inventory, priorities and ordinary Practice execution

[Full target inventory](hook-inventory-beta30.json) records each class/function, exact
GD 2.2081 iOS binding, inline/debug status, ordinary Practice condition, hot-path flag
and pre-beta.29 presence. Bindings: `d641858f3e1cd2c6d86013c799f224ebdb8ec1cd`.
Pre-beta.29 source reference: beta.28 `c662f30c75480d899167f3f786802748065b71b2`.

| Group | iOS detours | Modify classes |
|---|---:|---:|
| A: native probes | 10 | 2 |
| B: PlayerObject | 7 | 1 |
| C: GJBase world/camera | 3 | 1 |
| D: persistence | 2 | 1 |
| E: PlayLayer/UX/EndLevel/App audio | 23 | 4 |
| UI/metadata retained | 11 | 3 |
| Debug ON | **56** / 53 distinct addresses | **12** |
| Debug OFF | **46** / 44 distinct addresses | **10** |

PostUpdate and togglePracticeMode each have two Corum detours; Debug teleport shares
the C target. Counts distinguish detours from target addresses. Inline playerTouchedObject
is excluded on iOS/Windows; updateSpecial is excluded on Windows, preserving its beta.29
inline Follow-Y compatibility fix. Startup registry is the check of realized hooks.

B's update/rotation/spider and all C hooks predate beta.29. Beta.29 added B's global
setPosition/setRotation/setVisible/updateSpecial and broadened ownership guards. A
probes and D's storeNewLocalScore are beta.30 additions and cannot alone explain a
verified shared beta.29/beta.30 signature. Other modifier files match beta.28 byte-for-byte.
There is no Corum SpriteAnimationManager hook or private Tulip/manual native-hook path.

Original-call audit: B/C ordinary branches delegate through qualified Base binding
once; no same-function ModifiedClass recursion was found. C's spectator-only
updateCamera fallback is a cross-function call when native omitted camera, guarded
to one calculation/frame. Driver setter reentry is scoped. Ordinary Practice does not
take the fallback or capture branches. Early return still traverses generated handler
post-return cleanup; no manual index operations, throw or longjmp exist in these bodies.
Foreign callbacks/allocation exceptions/native runtime defects remain possible.

Priority audit: each current Priority::First name matches an address-bearing iOS target;
platform inline exclusions are present. Failure currently warns/continues; diagnostics
count and name failures. No bad priority target was found in source; device registry logs
remain pending. Native hook inventory excludes helper methods without generated binding
entries rather than assuming every member in a modify class becomes a hook.

Ordinary path: PlayLayer init creates Fields and sets rankedInitialized=true even with
rankedLevel=false. postUpdate performs safety synchronization, vanilla and runtime tick
before its Ranked early return. Non-Ranked safety stays inactive; Practice capture is
rejected and there is no local Ranked attempt to send. Gameplay UX calls ensureLabels
before checking Ranked, so ordinary scenes can allocate labels. This real code path
was not speculatively removed. Real Practice saves remain allowed.

Unattached remote/camera/driver members stay empty in ordinary scenes. Remote cleanup
guards empty Refs; camera mutates world only if attached; driver reset clears its raw
owner; scene/TLS owners have RAII restoration. No active spectator callback owns a fresh
ordinary Practice scene. This source audit does not eliminate heap corruption; inspect
the group implicated by device isolation before choosing a memory fix.

## Disable semantics and future fix constraints

[HookImpl disable](https://github.com/geode-sdk/geode/blob/7e41336f68660b990644f5c3450b6da6ae944560/loader/src/loader/HookImpl.cpp#L82)
removes a detour/decrements users. In Tulip runtime-intervening-disabled mode, the last
removal does not restore a prepatched function: Pool/handler/common-handler/TLS machinery
remains. A compiled-out group removes Corum detours, not other mods or Geode wrappers.

A/B/C/D preprocess out their entire modify declarations. E clears public m_hooks in
onModify before claimHook/enable, verified in SDK 5.8.2 and runtime 5.10.1 sources;
its PlayLayer Fields type remains for sibling groups. No new native hooks were added.
Package modify-marker checks and startup public API registry establish the distinction
from merely adding if(!ranked). Normal logs saturate at three entries per function.

No production hook reduction has been finalized. If B is implicated, split the four
beta.29 setter/special hooks from update/rotation/spider. Consider frame capture and
scoped reconciliation only with native ordering/safety evidence. Preserve exact native
Spider/Teleport endpoints; never infer verified snaps from position distance alone.
If C is implicated, establish world/camera order before moving capture to postUpdate;
PlayLayer inherits some GJBase functions, so renaming the modify target does not itself
create a narrower address. If D/E is implicated, split that group while retaining real
Practice/Placement saves and timer-zero active final attempts.

Require repeated fixed-device A/B exposure plus Ranked/Placement/RECORD/TEST/Live Spectator/
Spider/world/camera/Safe Mode regression before a clean beta.31 main patch. Protocol v2,
snap segments, adaptive polling, stale Attempt protection and persistence fix are preserved.
