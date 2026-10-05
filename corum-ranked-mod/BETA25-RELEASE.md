# v0.5.0-beta.25 — Live Spectator Phase 4 gameplay state

Baseline: remote main `b948544c7e8288a186c0df6df880dc47a96d6a70`, beta24.
The mod manifest and server exact client gate both advance to beta25.

Production protocol v2 and Debug format v2 add all eight GD modes, native mini,
gravity and visual flip. Shared native capture/codec feeds strict server relay and
the dedicated render-only Remote Player. Native vehicle/size helpers run on state
transitions; death fades, new Attempt respawns, Clear holds its final pose. Native
camera follows vertical motion and bounded Spider gravity snaps. Local m_player1
never receives remote state; all Phase-1 transport/completion/progress-save guards
remain independent. No score/rating/Achievement/AP/title policy changes are made.

Debug VOICE RECORD/TEST uses the same state through actual HTTP, existing JSONB
bulk persistence, server replay clock and production relay/viewer/renderer/camera.
Old format-1 recordings remain stored but are not replay-compatible. No migration,
local replay, fabricated match, synthetic level/song or direct injection is added.

Tests cover native mode capture/application for all eight modes, transition-only
initialization, size/gravity/flip, immutable terminal/context respawn, strict v2
codec and stale packets, bounded Spider snaps, remote vertical/terminal camera,
real HTTP/PG expanded fixture persistence/playback/restart/version selection,
unchanged Ranked tables, dummy New Best protection and Official Debug OFF.
Terminal tests cover cadence/Start/live ACK ordering and bounded End/HTTP races.

Dual is explicitly detected with DUAL_UNSUPPORTED fallback, not rendered. The map
pool lacks reliable portal metadata, so no assertion that Ranked has no dual is
made. Player2, cosmetics/full trail/effect parity, advanced interpolation/reconnect,
complex teleports/camera triggers/platformer and device E2E remain Phase 5.

Real iPadOS/Windows/two-client GD E2E has not been performed in this environment.
Required device QA: RECORD real VOICE Cube/available mode portals/mini-if-present/
gravity/rotation, a death fixture and Clear fixture if possible. TEST each fixture:
confirm separate Remote Player mode/orientation, vertical camera, death fade/new
production Attempt respawn, visible final Clear hold, hidden safe dummy and no
TEST New Best/percentage changes. Modes absent from VOICE have automated coverage
only. RECORD itself is vanilla and may save legitimate New Best.

No Release workflow, GitHub Release or unnecessary CI rerun is part of this patch.
Automatic CI IDs/results are recorded in the final report and split Canonical.
