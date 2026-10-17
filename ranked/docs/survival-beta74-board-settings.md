# Survival board and native settings — beta.74

Base: `main` at `9e923247845a0b7b4f250625121349581966ecbb` (beta.73).
Branch: `feat/survival-board-ui-settings-beta74`. Main was rechecked before work;
no newer commit or open PR was present. Existing branches, including
`work/live-spectator-20260911`, are retained. The user subsequently authorized
main integration. Production deployment and R2 publication remain excluded.

## Causes and implementation

| Area | Finding and resulting behavior |
| --- | --- |
| Board clock | The retained board had no persistent deadline label. It now owns one top-right label, using the same `ServerClock` and `startsAt`/`endsAt` as the HUD. Playing shows `ROUND MM:SS`; countdown shows `START IN MM:SS`. Selection/preparation/entry/result show states. Missing clock/deadline shows `--:--`, never a guessed 180 seconds. Poll errors do not replace the clock. Final results hide it. |
| Navigation | The board condition required a normal clear, and Back popped the coordinator. Back from native gameplay still uses the scoped deferred quit to map preparation; Back there now opens the retained board with no FORFEIT. BACK TO MAP preserves preparation and requests a fresh server snapshot before enabling re-entry. An additional Back opens explicit surrender confirmation. Expired/unknown playing time, terminal rounds, elimination and Death Match's three-attempt limit block re-entry. |
| Native return | The deferred return already checked launch generation and origin. It now also retains a weak departing scene and verifies that it still owns the running scene with no pending transition before popping. Duplicate native return remains consumed once. No destruction is performed in a PlayLayer callback. |
| Names | Database/session/Survival projections preserve the submitted GD name; no forced uppercase conversion was found there. The existing emphasized fallback font can substitute uppercase glyphs. Player-name labels now use Corum Black with lowercase-capable `chatFont.fnt` fallback, and the HUD displays the original own-player name as well. Existing Black TTF has distinct ASCII lowercase glyphs and is unchanged. The reported device rendering was not reproduced here. |
| Map length | Both native UI metadata and server enrichment interpreted GD verification timestamp/key 57 as authored seconds (`/240`). That inference and its duration-only download request are removed. Explicit seconds retain priority, then native TINY/SHORT/MEDIUM/LONG/XL, then UNKNOWN LENGTH. The metadata cache uses a new namespace so old inferred durations are not reused. Solo shares the same display helper. |
| LDM/Shake | Preparation resets its per-context LDM flag, replaces native level pointers, and previously received the Survival preferences only once from the UI. Survival now reads its persistent preferences during preparation and reapplies them before native scene creation and PlayLayer init. Native camera-shake suppression is scoped to official Survival. LDM visibility requires ready native data for the exact map and the author's `m_lowDetailMode`; unsupported maps do not erase the preference. Remaining option buttons reflow. Debug builds log values before/after init. |
| Mod switch | `modRestrictionEnabled` defaults TRUE from Sheets through validated snapshots, public config, rules and native policy. FALSE bypasses only the generic mod allowlist/required/version/CBF policy. Lists remain intact. A generation change publishes each switch. Client polling updates policy/warnings without resetting sessions/matches; Recheck reloads config if admission was blocked before a session existed. CBF/Node IDs are optional Geode loader dependencies so FALSE can actually load without them; TRUE still enforces their existing rules at admission. Corum's independent client version guard, session/auth, evidence, scores and HAC remain in place. |
| Card colors | Existing cards retain identity and movement. Retained accent surfaces follow authoritative elimination first (red), then rank 1 gold, 2 silver, 3 bronze, else the base style. Unknown rank has no medal; ties share color. Own-player identity remains a separate name accent. Final standings use podium accents; elimination overlay animation is retained. |

## Validation

Native harnesses compile production methods with strict C++23 warnings against
explicit Cocos/GD/HTTP doubles. They cover clocks with missing/invalid time,
network gaps and elapsed background time, navigation without forfeiting,
re-entry fences, LDM capability/options, policy transitions without match loss,
retained card recoloring/ties/final podium, case-preserving fallback, and
stale/foreign scene rejection. Existing Pause/Force Clear/surrender/Free Play,
HUD/BGM, Solo score animation, spectator and record tests remain included.

Server tests exercise the real generic switch at Solo queue, Placement start,
Survival online HTTP queue and Debug creation. They preserve independent
password/ownership/version/auth behavior and existing match settlement tests.
Survival online HTTP/concurrency/recovery is also run against an isolated
PostgreSQL 16 database. No production database is used. Final local results: Native 200/200, server 541/541, rules 246/246, GD relay
13/13, Python packaging/publication/assets 16/16, Survival Apps Script 4/4
(1,020 test cases), plus ten strict C++ domain executables and both standalone
Ranked/legacy Apps Script validators. PostgreSQL 16 reran 45/45 online tests.
Server/rules type checks and builds passed. CI links are recorded in the PR
and completion report.

The default five-second Death Match integration deadline was exceeded once
while native compiler and PostgreSQL suites ran concurrently. Its isolated
retry passed; the final full server run uses `--testTimeout=30000` without
changing test assertions or production timeouts.

## Platform and device limits

| Platform | Debug ON/OFF local build attempt |
| --- | --- |
| Windows | Both blocked during CMake compiler/linker checks: no Windows toolchain (`--major-image-version` unsupported by host linker). |
| Android64 | Debug ON and OFF native compilation/link succeeded with Geode 5.8.2 and NDK r29. |
| Android32 | Debug ON and OFF native compilation/link succeeded with Geode 5.8.2 and NDK r29. |
| iOS | Both blocked during configuration: no Apple `iphoneos` SDK on the Linux host. |

The Android builds use the manifest's Geode 5.8.2 SDK, GD 2.2081 bindings,
NDK `29.0.14206865`, Release optimization and both Debug feature flags ON/OFF.
All four produce the correct ARM/AArch64 ELF shared libraries. Debug OFF
binaries exclude the Force Clear dialog and SurvivalOptions diagnostics.

These are native compilation/link checks with `GEODE_DISABLE_CLI_CALLS=ON`
and the mod ID explicitly defined. Geode CLI resource/font generation,
`.geode` packaging and GD device execution remain unverified. Native binary
hashes are retained with the local validation artifacts; no binary is published.

Hosted [Ranked checks](https://github.com/ybaf100/Corum-Ranked/actions/runs/38921332527)
and [platform CI](https://github.com/ybaf100/Corum-Ranked/actions/runs/38921332681)
failed before runner allocation (`runner_id: 0`, zero executed steps); downstream
platform jobs were skipped. Log retrieval returned `BlobNotFound`. The precise
hosted startup cause is unavailable; this is not a passing CI result. No
workflow, publication or deployment was manually dispatched.

Required GD device checks remain: supported/unsupported LDM maps and real LDM
object filtering; Shake Trigger with ON/OFF; pointer replacement, re-entry and
next round; iPad touch and Korean/English fitting; post-clear/Free Play clocks,
reconnect/background recovery; all native Pause/quit/audio transitions. UI and
native-value tests do not establish actual device effects.

## Operations handoff — not performed in production

Read-only searches in the connected Drive accounts did not locate the actual
Corum Ranked spreadsheet or its bound Apps Script deployment. A read of the
client's public server config endpoint timed out after 30 seconds without a
response. Therefore the live row value, deployed script version and deployment
ID remain **unverified**. Repository changes do not update an existing Apps
Script deployment automatically.

When production publication is separately authorized:

1. Identify the server's actual `RANKED_CONFIG_URL` and its bound spreadsheet /
   `CORUM_SPREADSHEET_ID`, without publishing credentials.
2. Update the bound Apps Script from the reviewed source; run the additive
   `setupCorumRankedConfig()` to append the default TRUE row while preserving
   existing values, allowlists, required-mod data and other tabs.
3. Validate the generated Ranked config. Check
   `data.operational.modRestrictionEnabled` is a boolean and that changing the
   row changes `generation` while CBF/allowlist data stay intact.
4. Update the existing web-app deployment to the reviewed new script version;
   retain its URL/access policy. Verify the actual deployed response before
   refreshing/deploying the server.
5. Verify server `/api/ranked/config` and new clients agree after TRUE/FALSE /
   FALSE/TRUE changes, and independently reject obsolete clients/invalid
   sessions. Keep active matches and evidence intact.

This change creates no migration and performs no Sheets edit, Apps Script
deployment, Render deployment, R2 upload, package-path overwrite, or unrelated
repository change. Main integration uses skip markers to avoid automatically
publishing/deploying unverified platform packages.
