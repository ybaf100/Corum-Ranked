# Corum-Ranked v0.5.0-beta.98 production rollout

Source base `41fbc5f257be85de72b4705708e7492047e925fe` (PR #34); user authorized beta.98 client and Singapore server deployment.

- Synchronize client manifest, server required/previous versions, npm workspace manifests and lockfile.
- Stage immutable Release and Debug Geode R2 publication first via `[publish-r2] [no-deploy]`, verifying both workflows and public descriptors before activating the Singapore server.
- Keep the same four platform payloads (Windows, iOS, Android32, Android64), preserving prior beta.97 objects. A failed repeated publication of an existing immutable version must not be treated as a binary build failure.
- After R2 verification, a separate `[deploy-lightsail] [build only]` docs-only commit deploys the exact beta.98 server through the established pinned SSH, Docker health check and rollback.
- Do not deploy retired Seoul or turn the Railway bridge into a game writer. No DB reset or unrelated Neon changes. Retain production LP/MMR/matches/attempts/achievements.
- Check live `/health`, `/ready`, `/api/ranked/config`, the Railway update bridge, and note that native device behavior must be confirmed separately.

## Release progress

- [ ] Release R2 publishing successful
- [ ] Debug R2 publishing successful
- [ ] Public descriptors and immutable package hashes verified
- [ ] Singapore Lightsail server activated and healthy
- [ ] Live version `v0.5.0-beta.98` verified
