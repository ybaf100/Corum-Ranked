# v0.5.0-beta.22 — Server-backed Debug Spectate

Builds on main `76414914f717b813812c1a6f4bc200d5800ded65` (beta21, Phase 1–3 safety/remote/camera). The exact client/server gate advances for this patch; workspace package versions retain their existing independent convention.

RECORD SPECTATE and TEST SPECTATE are development-only, fixed VOICE (88350599). RECORD uses real vanilla GD preparation/gameplay and HTTP pose upload into isolated PostgreSQL debug tables; TEST uses a scoped server replay clock feeding the production live relay/HTTP decoder/interpolation/Remote Player/camera. No local replay JSON/file/clock, no renderer bypass, no production Match forgery or Ranked data awards.

Server defaults OFF and requires explicit deployment flag + debug secret + current session at creation + scoped short-lived token afterward. Debug CI compiles ON, official package compiles OFF. All Phase 1 local dummy guards remain active in TEST, independently of remote visual/camera; RECORD is normal non-Ranked vanilla gameplay.

See [server-backed harness API, lifecycle and exact device checklist](../ranked/docs/debug-spectate.md). Local listening-HTTP/disk-PG and native-double verification are not actual GD-device or deployed-server certification. Camera remains basic classic follow; Phase 4 modes/gravity/mini/dual/death-respawn/full Clear and complex-trigger parity, Phase 5 cosmetics/interpolation/E2E and Phase 6 optimization are not implemented here.

Only automatic push CI is inspected; no manual Release, GitHub Release, workflow rerun or force push.
