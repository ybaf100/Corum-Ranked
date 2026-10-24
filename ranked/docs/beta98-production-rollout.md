# Corum-Ranked v0.5.0-beta.98 production rollout

Source base `41fbc5f257be85de72b4705708e7492047e925fe` (PR #34); user authorized beta.98 client and Singapore server deployment.

- Synchronize client manifest, server required/previous versions, npm workspace manifests and lockfile.
- Stage immutable Release and Debug Geode R2 publication first via `[publish-r2] [no-deploy]`, verifying both workflows and public descriptors before activating the Singapore server.
- Keep the same four platform payloads (Windows, iOS, Android32, Android64), preserving prior beta.97 objects. A failed repeated publication of an existing immutable version must not be treated as a binary build failure.
- After R2 verification, a separate `[deploy-lightsail] [build only]` docs-only commit deploys the exact beta.98 server through the established pinned SSH, Docker health check and rollback.
- Do not deploy retired Seoul or turn the Railway bridge into a game writer. No DB reset or unrelated Neon changes. Retain production LP/MMR/matches/attempts/achievements.
- Check live `/health`, `/ready`, `/api/ranked/config`, the Railway update bridge, and note that native device behavior must be confirmed separately.

## Release progress

- [x] Release R2 publishing successful
- [x] Debug R2 publishing successful
- [x] Public descriptors and immutable package hashes verified
- [ ] Singapore Lightsail server activated and healthy
- [ ] Live version `v0.5.0-beta.98` verified


## Verified immutable package publication

- Source commit `2a8f2e2125b6e65adcae61edf8777e72497f254a`.
- [Release CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40108617304): all four targets and R2 publishing succeeded.
- [Debug CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40108617189): Debug ON/OFF target builds, package, tests and R2 publishing succeeded.
- [Source CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/40108617243): success.
- Public Release descriptor beta.98: 8,809,371 B, sha256 `25e66ee22c7177454e96a4f37a2ac0920040684ff52f05382454b81b235281a9`.
- Public Debug descriptor beta.98: 9,390,073 B, sha256 `dabaf09fff4b4318a5abfea75278ece761ea6bcddee8b1858681dc1c97966496`.
- Descriptors read from public R2 endpoints. Binary payloads were validated by their GitHub packaging/publishing workflows; independent full-download SHA calculation was not done.
- Pre-deploy read-only Neon production query: 0 ongoing Solo matches, 0 ongoing Survival matches, 0 queued in either mode.

## Activation

- The docs-only commit carrying `[deploy-lightsail] [build only]` activates Singapore only after both R2 channels succeeded.
- This deploy changes the server-required client version to `v0.5.0-beta.98`; no production reset or schema migrations are requested.
- Verify actual deploy run and live public version before checking boxes above.
