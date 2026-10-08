# Corum Ranked v0.5.0-beta.21 — Phase 3 native compile correction

This patch retains all Phase-3 behavior and pending device E2E boundaries in `BETA20-RELEASE.md`. Final installable Phase-3 candidate uses beta21's synchronized exact mod/server gate.

Beta20 commit `361d6c97834fb8e8111b282ec3dfd07fe7404880` passed automatic Validate run `37264962347`, but automatic Debug run `37264962327` found an actual code compile failure: the new const `watchableSpectatorScene()` helper accesses Geode Modify's non-const `m_fields` operator. The fix removes the unsupported const qualifier; no scoring/gameplay rule changes.

`SpectatorSceneNativeTest.mjs` now models Geode's exact non-const field accessor instead of permissive unique_ptr access and contains a negative compile proof reproducing the former failure. Camera, dedicated remote, actual decoder, Phase-1 guards and other source/domain/server tests remain required.

No force push/reset, failed workflow rerun, manual Release or GitHub Release. Each patch advances beta, including this correction. Real two-client Geometry Dash E2E is still unperformed; use beta21 on both devices for the exact checklist in `BETA20-RELEASE.md`. Camera-trigger/platformer parity and Phase 4–6 deferred items are unchanged.
