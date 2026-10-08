# Actual map difficulty, optional Jukebox, and external Ranked visuals

Base: candidate `cd0393195971443ec913b15c0ea89cf1dc2b5f51` on
`work/ranked-pause-quit-order-beta41`. Authoritative production main remains
`b418a75cd91aecbc6e787c09a8f6d5903870b874`.

The native Pause Quit candidate and private Ranked volume/Settings implementation
are preserved. This increment does not assert that surrender → next-game Pause
is device-fixed. The latest native Quit candidate still needs device acceptance.
The user's completed music fade policy is not changed. Client/server exact gate
remains beta.41 and spectator/recording protocols remain v2.

## Map difficulty

Ban candidates, round preparation, Placement maps and pool references now show
the actual GD difficulty face and label, including Easy/Normal/Hard/Harder/Insane,
Auto and Demon subtypes. The existing Corum rating remains a separate number.
Faces use GD's bundled frames; no new image download is needed for them.

Appearance-only GD metadata queries use the existing keyed response hook and
temporarily restore its foreign delegate. Current-map attestation has priority;
the same keyed query cannot collide with UI batch/single-ID lookup. The UI cache
contains values, not retained level objects or geometry, and grants no entry or
scoring authority. Unknown data displays N/A. Native mapping references are
GD 2.2081 fields and frame names; this is not an iOS disassembly claim.

## Jukebox

Reviewed official [Fleeym/jukebox v3.8.0 source](https://github.com/Fleeym/jukebox/tree/bdc2b89bc8675c399483a9cb052c66ac6b8aebc4),
especially [the native song-widget callback](https://github.com/Fleeym/jukebox/blob/bdc2b89bc8675c399483a9cb052c66ac6b8aebc4/jukebox/jukebox/hooks/custom_song_widget.cpp)
and [effective playback paths](https://github.com/Fleeym/jukebox/blob/bdc2b89bc8675c399483a9cb052c66ac6b8aebc4/jukebox/jukebox/hooks/music_download_manager.cpp).
Mod ID remains `fleym.nongd`. Its source is read-only and no code is copied or
linked from its private implementation.

The entry appears only when Jukebox is loaded. An attested current map supplies
its native CustomSongWidget (including RobTop and multi-song classification).
The integration activates Jukebox's own `fleym.nongd/nong-button` callback after
checking owner/selector. No public popup-opening API was found; no private cast,
function address, extra native detour or callback retargeting is introduced.
Absent callback data stays disabled. Its carrier widget's menus cannot receive
touch input. Open modals block native round launch; Corum's modal unregisters
only its own priority and tracks dialogs created by its exact opening callback.

This is song-ID management. Jukebox assigns its optional verified-level
recommendation ID only in its native LevelInfoLayer hook; that private setter is
not called by this integration. The current effective NONG path is already
checked by Ranked preparation without overriding vanilla GD music settings.

## External pack

All 61 large PNGs remain versioned in the source repository and are excluded
from new `.geode` packages. Fonts remain bundled. The deterministic builder emits
one ZIP plus its manifest/metadata. The dedicated host is
`ybaf100/corum-ranked-assets`, with an exact server-selected immutable tag.
There is no `latest` lookup, per-image HTTP request or production URL placeholder.

The existing resource screen distinguishes a new image download from an update
and shows installed → requested versions. One Download All action starts the
single image ZIP and required existing audio resources. Continue is blocked on
missing/failed packs, including when audio is disabled. The loader validates ZIP
size/hash, flat allowed entries, manifest/version, every file size/hash and cached
contents. Configuration is idempotent; stale generations cannot activate a pack.
Hashing/extraction runs off the UI thread. Activation preserves the current pack
until verified replacement and marker write complete, and removes its previous
version after success. Small GD fallback visuals keep old/disabled configs usable.

Read [ASSET-PACK-RELEASE.md](ASSET-PACK-RELEASE.md) for upload, Apps Script/server
configuration and immutable release rules. No external release or live config
has been published as part of this source change.

## GitHub redirect download correction (2026-10-08)

Both supplied device logs reject the pack with
`release returned HTML instead of the asset ZIP`. The published `visuals-v1`
release, the live server configuration and an independently downloaded ZIP all
agree: **24,649,307 bytes**, SHA-256
`9c91279a1a11406217a29ee210f6fbb10bdce461bb83f7e4ef7a0f6bfa3762a0`.
The actual ZIP passes CRC, inventory, manifest and every file's size/hash checks.

GitHub returns **302 `text/html` → 200 `application/octet-stream`**. Geode 5.10.1
keeps headers from every response; `WebResponse::header()` returns the first
value. The previous Corum callback mistook that redirect header for the ZIP's
content type and rejected the successful download before inspecting its body.
It now passes the saved body through the existing size, ZIP signature, SHA-256,
archive and manifest verifier. Genuine HTML and corrupt downloads remain rejected.

A new executable regression runs the actual download callback and background
size/signature/SHA verifier. It fails on the previous source and passes after this
correction, including absent content type, actual HTML, truncated/corrupt payload,
HTTP failure, save failure and stale HTTP completion. HTTP/archive/manifest and
main-thread boundaries are doubles; this is not iPad installation verification.
The existing uploaded pack and server metadata do not require replacement.

## Validation

- Source/native boundary suites: **112 passed** (includes previous Pause/volume,
  spectator safety, native preparation and transport suites).
- Strict C++ policy executables: **9 suites passed**.
- Pack builder/verifier: **5 Python tests passed** (synthetic PNG boundary data;
  real repository pack validation is also required in CI).
- Ranked rules: **114 tests passed**; server/database: **202 tests passed**.
- Ranked workspace type-check/build and additive Apps Script/legacy compatibility
  checks passed.
- Automatic Debug CI must build Windows, Android32/64, iOS Debug ON/OFF, verify
  packaged font/hook/image separation and produce the complete real image ZIP.

Host doubles and CI compile/package results do not establish native Jukebox or
Pause acceptance on an iPad. No production-main merge is justified by CI alone.
