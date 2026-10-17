# Survival map selection and surrender — beta.76

Implementation baseline: remote `main` at `f72edc4230d0a6ab401093c944e3475fee6e3ce0` (beta.75), rechecked before publishing. Work branch: `feat/survival-map-selection-roulette`. Existing branches and the separate Live Spectator work are preserved. The user's later instruction authorizes merging this work to `main`; the handoff's earlier restriction on operational Render, Sheets/Apps Script and R2 publication remains in force. Commit and merge titles use `[build only] [skip render]`, retaining the existing three workflows without awaiting their completion.

## Presentation and server ownership

`SurvivalMapSelectionTimeline` uses monotonic elapsed milliseconds, with a 4,000 ms terminal boundary shared by both modes. The actual server `map.levelId`, match, round and map generation determine the result; preview images never select a map or submit a gameplay event.

| Time | Thumbnail mode | Numeric mode |
| --- | --- | --- |
| 0–1.05 s | Five clipped horizontal cards, about 0.17 s/card | One fixed horizontal row containing every actual ID digit; each column rolls vertically |
| 1.05–2.15 s | Continuing carousel with center emphasis | Columns lock from left to right, at most 0.18 s apart |
| 2.15–2.75 s | Ease into the actual server-selected image | Final columns lock by 2.70 s |
| 2.70/2.75–3.20 s | Selected image shrinks and moves left | Complete golden ID pulses and moves upward |
| 3.20–4.00 s | Detail panel fades/slides in | The same controls appear in a wide panel without any thumbnail |

Native map/audio preparation runs concurrently. After 4 s, unfinished assets show a preparation status rather than restarting the animation. A known server countdown within the remaining reveal duration plus 500 ms, or `PLAYING`, skips directly to the final panel. Missing server clock synchronization does not invent a start deadline. Existing manual re-entry, fresh-server-sync checks, deferred native launch and scene ownership checks still apply. A preparation failure exposes RETRY, not a fictitious playable READY state.

Same-context board navigation/recreated layers consume the original timeline and sound events. Context changes invalidate old UI and requests; backgrounding invalidates the provider generation and application epoch. Retained GL cache entries are retired on the next visible UI activation, not from the suspension hook. Late replies cannot attach an old map's image to a new round.

## Visual layout

The implementation reuses Corum surfaces, navy/cobalt backdrop, Cyan/Blue/Gold, CorumSans fonts, difficulty/featured badges and existing translations. The reveal header is NEXT UP! / MAP SELECTION, with restrained rays and borders. The final thumbnail layout places a 16:9 image at left and metadata/controls at right. Compact viewports move controls across the full panel. Numeric mode has a golden ID above a broad information panel and a small localized Level Thumbnails hint below it. VIEW STATUS and SURRENDER remain separate footer actions. A server start/round clock has its own upper-right space.

Titles preserve original case and fit their reserved bounds. Creator names use UTF-8-safe one-line ellipsis. Metadata uses native/server-confirmed fields and the existing duration/length-category fallback. Unsupported LDM is hidden; the row reflows. Green ON/gray OFF switches, Jukebox, readiness and the yellow server-authorized READY have separate rectangles. Single-line button labels stay within their visible touch targets.

Pure layout tests cover `400×280`, `400×320`, `512×384`, `520×320`, `568×320`, `568×426`, `640×360`, `854×320` and `1024×576`, both modes and LDM supported/unsupported. They check panel containment, all text/control pairwise overlaps, thumbnail separation, 16:9 proportions, and minimum tested control sizes of 48×28 virtual pixels. Numeric tests additionally check all 8/9/10 columns remain horizontal while their digits move vertically.

`corum-ranked-mod/tools/preview-survival-selection.py` renders a static layout aid from the production C++ rectangles, with the actual CorumSansBlack TTF. Six Korean/English small/4:3/wide previews were generated and inspected. This is not a Geometry Dash GL screenshot: native badges and other font weights are approximated, and physical-device touch/Retina masking remain unverified. Preview images and fetched thumbnails are not included in a release package.

One deliberate UI limitation: the Jukebox box opens the existing native Jukebox song selector. Its track arrows and current-song UI remain in that selector; they are not duplicated inline through a private ABI. The existing integration documents no supported cross-mod popup/track-control API. The round's real Jukebox functionality and ownership are retained.

## Optional thumbnails

Official sources checked:

- [level-thumbs-mod](https://github.com/Level-Thumbnails/level-thumbs-mod/tree/b1a91ddb67ead73df98dd0f76fbc110b5389d89f): `cdc.level_thumbnails` v2.6.1, default API setting, internal `ThumbnailManager`, public CCImage decode/texture upload pattern and ImagePlus dependency.
- [thumbnail server](https://github.com/Level-Thumbnails/server/tree/51e0e5824127436a15826abb2bc8759e8f89d4d1): documented routes, relative 302 random Location, WebP sizes and 404 behavior. Server license is Apache-2.0; no explicit request-rate policy was located in the inspected source.
- [ImagePlus](https://github.com/Prevter/ImagePlus/tree/b88482252b2eb89cc496ddf2313379853af433dc): v1.1.1, MIT license, public CCImage format extension. No third-party implementation or asset is copied into Corum Ranked.

Both `cdc.level_thumbnails` and `prevter.imageplus` must be actually loaded through Geode. Neither is a mandatory Corum dependency. Disabled/unloaded/missing mods use numeric mode and make no thumbnail HTTP requests. No internal `ThumbnailManager`, vtable, hook address or private ImagePlus ABI is called. Decoding uses the public `CCImage::initWithImageData` entry point extended by the loaded ImagePlus mod.

Anonymous HTTPS requests use only `https://levelthumbs.prevter.me`, positive numeric IDs and a fixed user agent. They do not use the Ranked request builder or include session tokens, account IDs, names or authorization. TLS validation stays enabled. Random responses must be HTTP 302 with a canonical same-origin `/thumbnail/{id}/small` Location; automatic redirects are disabled. Foreign origins, query strings and malformed IDs are rejected.

Limits and failure behavior:

- Selected medium/small cache checks are prioritized; up to seven unique small previews are prefetched. Two HTTP slots, at most fourteen random requests and twenty-four total HTTP requests per context.
- Four-second request timeout and a 5.5 s main-thread cancellation guard; one retry after 1 s, 404 negative cache for 5 min, other final failures for 10 s, at most 128 negative entries.
- At most 2 MiB encoded per image. RIFF length/chunk boundaries, dimensions, bitstream count and animation flags are checked before decoding. Maximum 1280×720; animated/malformed/inconsistent streams are rejected.
- One worker decode at a time, at most three encoded images queued. CCImage CPU decode and encoded-file reads/writes run on the worker; GL texture upload and bounded cache eviction run on the main thread. Disk writes use generation-gated temporary files.
- Provider texture LRU: twelve entries / 16 MiB; disk: thirty-two images / 32 MiB. The visible UI separately retains at most eight small previews plus the selected image, so 16 MiB is the provider cache limit, not an assertion about total process GPU memory.
- Missing previews by 1.85 s or a missing selected image by 2.15 s trigger numeric fallback without resetting the four-second deadline. A late medium image can replace the selected small texture without changing map identity.

Public API smoke measurement from this Linux environment: random 302 in 754 ms; small WebP 640×360, 35,202 bytes in 542 ms; medium 1280×720, 81,036 bytes in 632 ms. These are network sample measurements, not in-game FPS measurements. Debug logs record cache/network source, bytes, download/decode/upload time, frame gaps above 50 ms and reveal elapsed time for device follow-up.

## Sound and options

Numeric locks dispatch the existing native `clickChange.ogg` once per digit. Thumbnail mode dispatches one center confirmation. A 65 ms catch-up interval avoids a same-frame burst; mute, duplicate polling, recreated scenes, server skips and background recovery consume rather than replay stale sounds. Gain is 0.22 times Ranked volume and passes through the native effects mixer. No BGM ownership changes or external sound asset were introduced. The requested 60–120 ms asset duration and actual mixer clipping have not been measured on a device.

Timeline tests cover 8/16/33/137 ms frame steps, exact completion at 4,000 ms, incomplete state at 3,999 ms, no elapsed drift, and the actual production sound dispatcher. The two modes share that exact domain boundary; first visible completion on a device is frame-quantized. No physical-device animation-duration measurement is claimed.

Ranked Settings now contains Survival defaults alongside the existing BGM volume and forum refresh/cooldown. Fresh defaults are OFF/OFF. Existing `survival-low-detail` and `survival-disable-shake` values migrate once into `survival-default-ldm` and `survival-default-shake`. Settings saves on close/exit. Map switches modify only round overrides keyed by match/round/map generation; the same round retains them across board navigation and native object replacement. A new round/generation inherits current global defaults. Options are reapplied to downloaded/replaced native levels, immediately before `PlayLayer::scene`, and at PlayLayer init. Unsupported LDM does not erase the user's global preference.

## Additional surrender repair

Three concrete defects explain the reported unresponsive/misplaced actions:

1. `canSurrender()` excluded waiting-room `MAP_SELECTION`, despite the server accepting a FORFEIT before a map exists. It now includes that state while keeping session, pending, elimination, completed-round and free-play checks.
2. A closed `RankedConfirmPopup` could remain alive in WeakRefPool and retain its modal force-priority registration until destruction. It now releases only its own registration at `onExit`, once, so subsequent Pause/waiting-room taps are not blocked by an invisible dialog.
3. Pause used a separate corner menu with an X icon and a caption extending outside its touch rectangle. Survival surrender now uses beta.72's native center-menu route with a 96×36 Corum face containing the entire label. When Node IDs provides no layout, a bounded native-row layout is supplied. Force Clear also follows the registered native menu priority if Pause itself has no touch handler; repeated synchronization does not continually lower priority or supersede an open confirmation.

Confirmations remain deferred and one-shot, with weak owner and match/round/generation validation. Pending submission disables conflicting actions and displays a server-waiting status. Waiting-room FORFEIT sends no fabricated attempt or native quit. In gameplay, the existing attempt-end FIFO, FORFEIT ACK observer and deferred `queueInMainThread` quit bridge are reused; no immediate scene destruction, synthetic `levelComplete()` or duplicate pop was added. Rejections leave the player in the current server-authorized state. A duplicate accepted event cannot settle LP twice.

The new server HTTP integration case confirms waiting-room MAP_SELECTION with `map=null`, a 201 FORFEIT ACK, rank 8 / −55 LP in the test fixture, identical-event idempotency and unchanged Solo profiles. Native tests exercise waiting-room confirmation, absent Pause-handler priority, retained closed popups, repeated clicks, retry/rejection, gameplay ACK without another poll and exact-owner deferred return. Physical iPad touch and the previously supplied AudioEffectsLayer/texture-cache crash still require device retesting.

## Validation performed

| Check | Result |
| --- | --- |
| Native regression suite | Full 213/213 passed before the final surrender additions; 3/3 new production sound cases passed, giving 216 distinct validated tests |
| Final affected Native regressions | 35/35 Pause/exit/board/debug tests; 19/19 selection/options/provider/sound/Pause tests; final provider resume change rerun 3/3 |
| Rules | 246/246 passed |
| Server | Full run 540/541 passed; one existing Deathmatch integration case hit its unchanged 5 s timeout under build contention. The same isolated case passed on retry. New waiting-room surrender case passed, giving 542 distinct validated server tests |
| Workspace typecheck and build | Passed; final typecheck includes the new server test |
| Whitespace | `git diff --check` passed |
| Android64 Debug ON / OFF | Both final-source compile/link runs passed |
| Android32 Debug ON / OFF | Both final-source compile/link runs passed |
| Windows / iOS Debug ON / OFF | Local toolchains unavailable; unverified |
| Packaged `.geode` / font-atlas generation | Not run locally; Android builds used `GEODE_DISABLE_CLI_CALLS=ON` and produce native `.so` files |
| Physical Windows/Android/iPad gameplay | Not available in this environment; unverified |

Android validation used Geode SDK 5.8.2 / GD 2.2081 / NDK r29. Both Debug feature flags match ON/OFF in each configuration. Final native library sizes: Android64 ON 5,189,144 bytes, OFF 4,647,240 bytes; Android32 ON 3,750,188 bytes, OFF 3,332,656 bytes. OFF binaries contain neither the `/force-clear` route nor the Force Clear button ID/symbol. Existing unrelated compiler warnings remain.

Existing beta.75 board countdowns, BEST/NOW, podium/elimination colors, name casing, map-length fallback, two-step exit/BACK TO MAP, cleared-player FREE PLAY, server Force Clear authorization, deferred quit, round/BGM ownership and mod-restriction policy were retained. Native/server tests cover their relevant contracts. Solo LP/MMR rules, result-score background policy, placement, Live Spectator, ordinary GD progress/records, achievements and immutable R2 paths were not changed. These are regression-test results, not a claim of physical end-to-end coverage for every protected feature.

## Device follow-up and operator configuration

Retest iPadOS Pause/open/cancel/reopen/confirm, waiting-room MAP_SELECTION and PREPARING surrender, delayed/rejected ACK, scene return, Force Clear → BEST 100% → FREE PLAY, repeated round transitions and the earlier AudioEffectsLayer crash. Verify real WebP texture lifetime across background/foreground, UTF-8 font atlases, numeric 8/9/10 lock sounds at native mute levels, GL memory/FPS and both four-second reveals on actual hardware. Also confirm visible native LDM/Shake effects with supported/unsupported maps and resume music behavior.

When `modRestrictionEnabled=TRUE`, the operator can add enabled Allowed Mods rows for `cdc.level_thumbnails` and its supported dependencies with **Required=false**. Inspected versions were Level Thumbnails 2.6.1, `prevter.imageplus` 1.1.1 (dependency minimum 1.0.0), and `geode.node-ids` minimum 1.7.1. Verify the installed platform versions against current official metadata before changing production allowlists. These optional mods must not become mandatory. Existing `modRestrictionEnabled=FALSE` semantics, authentication, version gate and HAC policy remain unchanged.

No production allowlist, Render deployment, Sheets/Apps Script release or R2 upload is part of this change. Client/server beta.76 version metadata is synchronized in source; distributing beta.76 clients requires the operator to coordinate the matching server version gate through a separately authorized release.
