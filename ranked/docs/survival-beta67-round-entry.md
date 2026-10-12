# Survival beta67: native entry, cutoff HUD and bot balance

## Reported Round 2 crash

The supplied iOS beta66 crash is a SIGSEGV in GD's `CCTextureCache::addImage`, called by `AudioEffectsLayer::init` during `PlayLayer::setupHasCompleted`. The device log identifies Round 2 map 149920242 (Eventide), with verified native metadata, saved geometry and audio ready before `round-native-launch`. The crash happens inside GD initialization, before Corum's post-init Survival HUD is attached. It is distinct from the previously resolved GD HTTP map-search block.

The log identifies the failing read, but does not identify which writer invalidated that native object. The patch addresses the entry/lifetime boundary without skipping native map objects or audio effects:

- The coordinator reserves one pending launch and defers it outside its scheduler callback. The callback checks the exact round/map context, running coordinator scene, confirmation and server entry permission again. Stale requests, duplicate updates, closed scenes and changed rounds cannot construct a scene.
- Native preparation rejects setup reentry and launches during an outstanding native quit/return. Saved level and origin scene have strong scope ownership while GD constructs the PlayLayer.
- Preparation retains the shared texture-cache owner across native play/quit and inter-round roster presentation, releasing its guard when no Ranked/Survival match or owned scene remains. This does not disable cache cleanup or retain a second copy of every texture.
- Entry logs now include the current/retained texture-cache addresses and prior PlayLayer, followed by a native-setup-completed log when construction returns. These distinguish a failure during native setup from countdown/HUD failures if a device still reproduces it.

The native tests execute production preparation and deferred-entry bodies against API doubles. They check retained cache ownership across a return, pending-return exclusion, exact context/scene/modal rechecks and a retry after a failed launch. They do **not** reproduce GD's internal iOS fault or establish device-level resolution. The user performs real platform/package/device tests.

## HUD

The client reads the server's `targetQualifiers`. Its leading list extends through passing cutoff ties when possible, caps the leading entries at ten, and appends the human's own row only if absent. Normal targets are 10/6/3/1; ties remain authoritative even when the visible list is capped. A red separator marks the passing/outside boundary of the visible rows. Existing Solo fonts, integer normal progress, retained rows and rank/score motion remain.

The existing native GD progress bar receives two text-free markers: red at server `cutoffScore`, gold at the leading authoritative standing's score. Marker positions use the full native progress width and native fill/bar transforms, not the current shrinking filled rectangle or interpolated HUD score. A thinner gold stroke keeps red edges visible when the values coincide. A missing or hidden native bar remains missing/hidden; the patch does not change the user's progress-bar setting or Solo HUD.

## Bots

Normal-round candidates use Box–Muller N(50, 20²), clamped to 0–100 and rounded to integers. Each bot gets its own seeded initial draw. The five exact update times are 20/50/80/110/140 seconds. Each has a 40% candidate-draw chance; otherwise the record is held. A candidate below the recorded best is ignored, so scores never decrease. Selecting a new candidate is not a guarantee of improving an already high record.

Persisted per-round/map/bot event indices now cover 0 through 5 (initial + five updates). Tick/restart duplicates cannot replay them. Three immediate 0.001%-precision Death Match attempts and its uniform distribution remain unchanged. Debug LP settlement, shared GOLD, mode activation, account LP snapshots and public matchmaking policy remain unchanged. No DB migration or Apps Script change is needed.

## Validation

Client, server, workspaces, lockfile and version gate are v0.5.0-beta.67. No font input, glyph set or raster size changed. Local checks include TypeScript source/test checks, all rules tests, selected Survival/server/version integration tests, strict C++ policy programs, native API doubles and release/asset policy tests. No full Geode client build, Android64 package inspection, iOS/device test, production DB change or workflow wait is performed.

Local results: TypeScript source/test checks passed; 196 rules tests passed; 119 selected Survival/version server tests passed; 10 native API-double tests passed; all ten workflow policy programs plus the Survival HUD domain passed with strict warnings; 16 release/asset policy tests passed. The Death Match integration fixture now uses seed 57, which produces a legitimate tied 100% finalist under the nerfed distribution, rather than relying on the former uniform bot scores.
