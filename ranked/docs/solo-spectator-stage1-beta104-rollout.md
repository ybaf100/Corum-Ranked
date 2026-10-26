# Solo live spectator Stage 1 — beta.104 publication

Starting task main: `53a00958da3bb01d19fe235c61962770e4296d53` (beta.102).
Implemented source: `3164b1174215a2c9ca4346a0e6a5bad09cd4012f`;
Apple integer correction: `8016459f4778b251b40ebacad17ee6e826a93bb0`;
compiled-package contract: `30d9c4a7a905f499da220f0d2c09b8871c350ad6`.
[Implementation, exact native call order and evidence limits](solo-spectator-stage1-beta103.md).

## Why a fresh immutable version

All four Release builds, all four Debug builds and all four Debug OFF checks on
30d9c4a7 passed. Full native CI was 300/300, rules 288/288, server 630 passed /
1 optional skipped, PostgreSQL HTTP integration 52/52, with 1,050 simulated
matches and a ten-hour virtual soak. Typecheck/build and real package checks passed.

The earlier 8016459f Debug workflow published beta.103 even though its iOS Debug
OFF package check failed under the old verifier contract. Its native runtime is
identical to 30d9c4a7 (that next commit changes only tests/docs). The final Debug
publish therefore correctly failed closed because immutable bytes already existed.
Both public beta.103 channels were downloaded in full and verified; neither was
overwritten, removed, or activated as the server's required version.

To retain exact final-source publication provenance and a successful gated run,
this task continues as beta.104. Only synchronized version metadata/changelog and
the publication prerequisite change; spectator implementation is unchanged.
Debug publication now also requires ios-debug-off and additional-debug-off, so a
failed OFF platform cannot publish while Debug ON succeeds. A workflow contract
regression verifies the full dependency and fail-closed default Actions semantics.

The beta.104 source is frozen for one `[publish-r2] [no-deploy]` push. Both channel
builds/publication and Docker preparation run concurrently. Singapore activation
will follow only after both full public packages (hash/size/identity/channel,
native architecture/detours, fonts/assets) verify. Existing pinned-host, rollback,
DB and immutable-object protections remain. No production migration or data reset.

Physical Geometry Dash trigger execution and input/scene behavior remain untested
on devices. Full dual, arbitrary historical world catch-up, advanced camera /
platform parity and Survival opponent-selection are Stage 2 limits, as detailed
in the implementation report. Actual publication/activation evidence follows.

## Verified beta.104 client release and activation request

Frozen source: `966fd23f981e85f27b7d779d8598a2077c5226fd`.
All four source workflows completed on their **first attempt**:

| Workflow | Result | Run |
| --- | --- | --- |
| Release: four platforms, package, R2 | success | [40311202388](https://github.com/ybaf100/Corum-Ranked/actions/runs/40311202388) |
| Debug: four ON / four OFF, package, R2 | success | [40311202373](https://github.com/ybaf100/Corum-Ranked/actions/runs/40311202373) |
| Rules / server / PostgreSQL / Apps Script | success | [40311202318](https://github.com/ybaf100/Corum-Ranked/actions/runs/40311202318) |
| Docker preparation, activation intentionally skipped | success | [40311202325](https://github.com/ybaf100/Corum-Ranked/actions/runs/40311202325) |

Final-source CI: native 300/300, rules 288/288, server 630 passed / 1 optional
skipped, PostgreSQL 52/52. Typecheck/build, 1,050 matches / ten-hour virtual
simulation, software GLES shader compilation/link plus 120 frames, publication
policy 10/10 and packaging tests passed. No physical-device result is implied.

Full public package downloads independently passed descriptor/version/channel,
size/SHA-256, ZIP CRC/entry uniqueness, Windows PE / iOS arm64 Mach-O / Android
ARM32 and ARM64 ELF headers, compiled production/safety/Debug detour checks,
18 font bundles and external visual-resource separation.

| Channel | Bytes | SHA-256 | Public package |
| --- | ---: | --- | --- |
| release | 8966753 | `78e7b0852e380dabadf23bec85466ea55fe4b3c8afbd34d53ffd7bba69862da7` | [download](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.104/Corum-Ranked-v0.5.0-beta.104-release.geode) |
| debug | 9546207 | `203ae1834d88388b7b06d62098760542cc38f16b5f2c1ea9ae0b8df15cb606dd` | [download](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.104/Corum-Ranked-v0.5.0-beta.104-debug.geode) |

A read-only production baseline on 2026-10-26 10:56:30 UTC still has 2 accounts,
2 Solo / 4 Survival matches, 86 / 5,381 Attempts, 24 unlocks, 19 AP ledger rows,
total AP 335, unchanged activation epoch and profile digests, and zero inconsistent
LP ledger rows. Solo and Survival are ON; Survival remains 64 players.

This documentation-only main commit requests the existing `[deploy-lightsail]`
activation now that both channels verify; `[build only]` prevents redundant R2
publication. Its runtime code is exactly the frozen source above. Existing
workflow re-verifies both packages before image upload/rollback-capable activation.
Actual activation and public API results are recorded after the run.

## Actual modified files

- `.github/workflows/build-ranked-debug.yml`
- `.github/workflows/deploy-ranked-lightsail.yml`
- `corum-ranked-mod/changelog.md`
- `corum-ranked-mod/mod.json`
- `corum-ranked-mod/src/PlayLayer.cpp`
- `corum-ranked-mod/src/RankedRuntime.cpp`
- `corum-ranked-mod/src/RankedRuntime.hpp`
- `corum-ranked-mod/src/SpectatorLiveRuntime.cpp`
- `corum-ranked-mod/src/domain/SpectatorCameraPolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorHandoffPolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorNativeProbe.hpp`
- `corum-ranked-mod/src/spectator/SpectatorTriggerScope.hpp`
- `corum-ranked-mod/src/spectator/SpectatorWorldDriver.cpp`
- `corum-ranked-mod/src/spectator/SpectatorWorldDriver.hpp`
- `corum-ranked-mod/tests/ModReleasePublishTest.py`
- `corum-ranked-mod/tests/SpectatorCameraOwnerNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorCaptureNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorHandoffNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorSceneNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorViewerPollingNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorVisibilityNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorWorldDriverNativeTest.mjs`
- `corum-ranked-mod/tests/helpers/SpectatorNativeDouble.hpp`
- `corum-ranked-mod/tests/verify-visibility-package.py`
- `ranked/apps/server/package.json`
- `ranked/apps/server/src/match/match.service.ts`
- `ranked/apps/server/src/match/spectator-live.policy.ts`
- `ranked/apps/server/src/session/client-version.ts`
- `ranked/apps/server/test/lightsail-deploy-policy.test.ts`
- `ranked/apps/server/test/spectator-authority.test.ts`
- `ranked/docs/live-spectator-protocol-v2.md`
- `ranked/docs/solo-spectator-stage1-beta103.md`
- `ranked/docs/solo-spectator-stage1-beta104-rollout.md`
- `ranked/package-lock.json`
- `ranked/package.json`
- `ranked/packages/rules/package.json`
