# beta.109 production activation request

Source: `c84aa84327c9aa78bbbca8545ab5b637e018c871` (the preceding implementation commit). This document changes no runtime code or operational configuration.

## Verified release prerequisites

- Validate: https://github.com/ybaf100/Corum-Ranked/actions/runs/40636111765 — success; Rules 288, server 675 (one default skip), isolated PostgreSQL 16 HTTP 59.
- Native: https://github.com/ybaf100/Corum-Ranked/actions/runs/40636111806 — 310/310, strict C++ domains, shader compile/link and 120 rendered frames; all four Debug ON/OFF builds and publication succeeded.
- Release: https://github.com/ybaf100/Corum-Ranked/actions/runs/40636111788 — all four builds, packaging and publication succeeded.
- Server image preflight: https://github.com/ybaf100/Corum-Ranked/actions/runs/40636111784 — prepare-image succeeded; activation was skipped during client verification.

Both immutable packages were downloaded in full and checked against public descriptor size/SHA-256, embedded mod identity/version/channel, ZIP CRC, all four native architectures, required production Random/trigger and read-only safety hooks, 18 font bundles and external visual separation.

| Channel | Bytes | SHA-256 | Public descriptor |
| --- | ---: | --- | --- |
| debug | 10667226 | `1777bb18b879b654437984e90ce8d5bf7beb2da01ac2f1ae4dc11845c08046ea` | https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.109/debug.json |
| release | 10014026 | `b10845cc35321f87c95d41df03dcce7d46cd6df310ecdf82307edf6686e4ac0b` | https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.109/release.json |

## Activation

This commit requests the existing pinned-SSH Singapore activation and rollback workflow only after the verified prerequisites above. The workflow independently verifies both public client packages before upload/activation and checks public readiness/version afterward. Its activation success log identifies the exact Docker image SHA. Requesting activation alone is not a success claim; the final observed Actions run and public API evidence are recorded in the task completion report.

Pre-activation public configuration: beta.108, Solo enabled, Survival enabled, 64 players. Preserve accounts, scores, records, achievements/AP, migration history, environment, secrets and rollback. No migration, data reset, Neon setting change, Railway or Render deployment is included.

Physical iPad/Windows Geometry Dash replay storage, audio, Random/Follow object parity and round navigation remain unverified. See [implementation/evidence](beta109-replay-world-stability.md) for confirmed defects, candidate causes and remaining device tests.
