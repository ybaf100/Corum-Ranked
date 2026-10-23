# v0.5.0-beta.94 Singapore migration and V/Mythic color correction

Production origin: `https://api.52-74-37-130.sslip.io`; Singapore Lightsail static IP `52.74.37.130`. Production Neon remains in Singapore: **do not reset or replace production data**.

Verified live endpoints before patch:
- `/health`: `status: ok`.
- `/ready`: `ready, rankedReady, databaseConnected, schemaReady: true`.
- `/api/ranked/config`: beta.93 and both mod update descriptors exposed.

Migration changes:
- All five native client entrypoints and endpoint regression checks now target Singapore.
- Railway bridge remains **upgrade-only** (read-only config, reject gameplay writes); its origin follows Singapore.
- Exact client/server gate and package metadata move to beta.94, all four platform targets must build and publish.
- V/Mythic card accents change from platinum to diamond ice blue `#8FE1FF`. V/Mythic progress bars reuse the existing seven-pastel flowing rainbow; Claim All, other stages and controls stay static.

GitHub Actions setup (already checked successful for Singapore via workflow run https://github.com/ybaf100/Corum-Ranked/actions/runs/39963074332):
- Environment `lightsail-production` variable `LIGHTSAIL_HOST=52.74.37.130`
- Verified `LIGHTSAIL_SSH_KNOWN_HOSTS` and `LIGHTSAIL_SSH_PRIVATE_KEY_B64`; `check-only` must succeed before deployment.
- Keep `LIGHTSAIL_AUTO_DEPLOY=false` until the release is approved; the deploy workflow refuses any old Seoul IP.

Release order:
1. Merge client/server changes only after CI. Do **not** use `[deploy-lightsail]` commit marker before immutable R2 beta.94 debug and release packages are ready.
2. Verify `Corum-Ranked-v0.5.0-beta.94-{debug,release}.geode` and descriptors exist on R2, hashes match actual files.
3. Manually run `Deploy Corum Ranked to Lightsail` with `mode=deploy` on `main`, then verify `/ready` and `/api/ranked/config` beta.94. Test actual Geode networking, matchmaking, LP persistence.
4. Previous beta.93 users hard-code the stopped Seoul `sslip.io` hostname, so they cannot receive automatic updates from Singapore without a separate **Seoul legacy update proxy**. Provide manual beta.94 packages or temporarily restore the old hostname with a read-only update/forwarding bridge.
5. Only after client access and production writes are verified, delete obsolete Seoul Lightsail instance and review static IP/snapshot charges.

No production DB migration or reset is part of this release. Do not print or commit secrets.
