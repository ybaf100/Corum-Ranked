# Singapore beta.94 production rollout — 2026-10-23

- Source revision: `eadb2ed3113fd0d3ed0b5e275ffd4c88a77ff299` (merged PR #27).
- Target: AWS Lightsail Singapore `52.74.37.130`, HTTPS origin `https://api.52-74-37-130.sslip.io`.
- GitHub Actions `lightsail-production` `check-only` SSH success: run 39963074332.
- Server validation: `Validate Corum Ranked` at source revision succeeded.
- Both beta.94 Debug and Release full platform CI succeeded; immutable Cloudflare R2 `debug.json` and `release.json` verified as publicly available on the release paths.
- This commit triggers server deployment with `[deploy-lightsail]` while `[build only]` suppresses a duplicate immutable R2 publication.
- No production Neon database resets or schema drops. The deployment must retain the single Singapore Neon production DB.
- Verify post-deploy `/health`, `/ready` (`databaseConnected`, `schemaReady`, `rankedReady`), and `/api/ranked/config` `requiredClientVersion=v0.5.0-beta.94` with both published R2 update descriptors.
- WARNING: beta.93 already installed clients hard-code the stopped Seoul `sslip.io` address and require manual beta.94 installation unless the old endpoint has a separate legacy update proxy.
- Do not delete Seoul Lightsail or backups until client and API tests are verified.
