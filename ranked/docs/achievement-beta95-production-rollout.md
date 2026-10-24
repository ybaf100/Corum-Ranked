# Achievement beta.95 production activation — 2026-10-24

Owner explicitly authorized production achievement reset, R2 publication and Singapore deployment after main integration.

- Package source: `9a50146c3857be548a8684f38f95b5f641adfd67`.
- Release Windows/iOS/Android32/Android64 and all Debug ON/OFF builds succeeded; both immutable R2 publications succeeded.
- Full public package downloads verified: Release 8588259 bytes SHA256 `9266eae43f71411b6b33f7e37c9bca9aee9253e5cae3f47398e90386cdbf5093`; Debug 9129749 bytes SHA256 `c25d867f7e48fa1870decb3c02181c2c14852a4960fc2898700b4cfe5bdb0b1a`. Both manifests are beta.95 and contain all four target binaries.
- Pre-reset Neon backup: `backup-before-achievement-overhaul-beta95-20261024`, branch `br-polished-smoke-azdncpop`, parent production `br-calm-wind-azkqms27`, project `bold-pond-55328690` (Singapore), ready with no compute endpoint.
- Pre-reset counts: 44 accounts; 45 Solo matches / 53 attempts; 83 Survival matches / 10630 attempts; 28 achievement unlocks and 865 total AP.
- Pre-reset Solo rating digest `60c18ee593db6488bac09d0914345913`; Survival rating digest `94db3d9f1ae34dc53202c70dc54fdb5f`.
- This commit activates the verified beta.95 server via pinned SSH on Singapore, preserving the single writer. Startup executes migration 0029 once; competitive data must remain intact. No deployment to retired Seoul.
- Verify actual workflow activation/rollback steps, live health/ready/config, production migration epoch and retained counts/digests after deployment. Physical-device gameplay remains a separate verification.

## Verified production completion

- Deployed source: `81a1a1750fd2861a0ffc1fae4bb9156773a94967`; [actual Lightsail build/upload/activate workflow](https://github.com/ybaf100/Corum-Ranked/actions/runs/40064447138) succeeded, including rollback-capable activation.
- [Release](https://github.com/ybaf100/Corum-Ranked/actions/runs/40063899164) and [Debug](https://github.com/ybaf100/Corum-Ranked/actions/runs/40063899213) builds, packaging and R2 publishing succeeded. [Source CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40063899111) succeeded.
- Production migration epoch: `2026-10-24T04:59:47.102Z` (13:59:47 KST). Both 0020 and 0029 are recorded in the migration ledger.
- Achievement unlocks/AP ledger/total AP/equipped titles/events/Survival facts/nonzero metrics: all zero. All 44 existing accounts retain a completed backfill marker, blocking pre-reset history resurrection.
- Preserved counts: accounts 44, Solo matches 45 / attempts 53, Survival matches 83 / attempts 10630. Both rating digests match the pre-deploy values above exactly.
- Singapore `/health` and `/ready`: HTTP 200; ready/databaseConnected/databaseReady/schemaReady/rankedReady all true.
- Singapore and Railway upgrade-bridge `/api/ranked/config` require beta.95 and expose both verified beta.95 descriptors. Railway still identifies as update-only bridge.
- Backup branch remains retained without a compute endpoint. No physical-device gameplay was claimed as verified.
