# Survival live spectator beta.107 — verified client publication and activation

Baseline main: `c3ad1d4b6352aaa72bbd587bc6fd26a0d1c8dc5f`.
Implementation: `2fba2667d359b5f882b1f6497e9da12b40467edd`.
Verified client source: `5c034382372d1dc8d27340a57428272c2a054925`.
Server terminal-lease fix: `79a0f9a534af21f34806326c3268ca0959a65eef`.

## Completed validation, 2026-10-27 UTC

- [Native/Debug/platform validation](https://github.com/ybaf100/Corum-Ranked/actions/runs/40482093898): success; 303 native cases, zero failures/skips, four Debug ON builds and four Debug OFF builds.
- [Server/rules/DB CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40482523428): success; 288 rules, 665 server passed and one optional pool case skipped; the separate real PostgreSQL 16 HTTP suite passed 58 cases. Workspace typecheck/build passed.
- [Release build/package/publication](https://github.com/ybaf100/Corum-Ranked/actions/runs/40482093899): success on Windows, iOS, Android32 and Android64.
- [Server Docker preparation](https://github.com/ybaf100/Corum-Ranked/actions/runs/40482523566): success without production activation.
- The first candidate's missing name-label argument failed native compilation; it was corrected before either immutable channel published. No failed package was activated.

Both actual public archives were downloaded fully and verified against descriptor size/SHA-256, mod identity/version/channel, ZIP integrity, all four native architectures, Stage 2/3/4 compiled markers, native persistence/visibility hook inventory, Debug instrumentation separation, 18 font bundles and external visual separation.

- release: [v0.5.0-beta.107](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.107/Corum-Ranked-v0.5.0-beta.107-release.geode), 9,305,100 bytes, SHA-256 `f2c6a107d35eb7b52f497c8ce6a75730aeccfbe6d6ff3eea8074d75565820d6d`; [descriptor](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.107/release.json).
- debug: [v0.5.0-beta.107](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.107/Corum-Ranked-v0.5.0-beta.107-debug.geode), 9,909,207 bytes, SHA-256 `5495dc27c560add9cdf4322fc1330e305fca34fa4ef043216dde3498097e1b5f`; [descriptor](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.107/debug.json).

## Activation

This documentation-only `[deploy-lightsail] [build only]` commit requests the existing Singapore production workflow after both verified channels. Workflow dispatch is not exposed by the connected GitHub tools; the repository's authorized push marker uses the same pinned-SSH upload, client verification barrier, rollback activation and public readiness/version verification. Client source and immutable bytes are unchanged by the server-only terminal retention fix and this record.

The production run and active image/source SHA, health/readiness and required version are checked after execution and reported separately. This record is not itself proof of activation. No migration, reset, data deletion, Neon/config/Secrets change, Railway/Render/Seoul deployment or protection bypass is included.

## Remaining device checks and inherited limits

Physical Geometry Dash/iPad/Windows UI touch, multi-device human native streams, long-run object lifetime and device FPS/heap remain unverified. Real human streams can be selected; server-simulated bots have no native publisher and remain unavailable. Demand capture cannot restore native trigger history never captured before the first viewer: that path stays PARTIAL SYNC. P2-only trigger parity and arbitrary full native checkpoints remain unverified. Bounded fixture/load results are not a production capacity, compute-suspend or device performance claim.

See [the Stage 4 implementation record](survival-live-spectator-stage4-beta107.md) for state separation, authority checks, budgets and regression coverage.
