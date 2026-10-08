# v0.5.0-beta.36 — higher-resolution Corum typography

Starting remote main: `6dbad86b949e3f317d5dcb13b7e0901b0b6d3482` (beta35).

The five existing Paperlogy UI weights use 128px source rasterization instead
of 64px. Geode now generates SD/HD/UHD fonts at 32/64/128px. Authored text
scales, width limits and minimum readability scales share a normalization
helper, including the main/lobby/results UI, gameplay HUD, LevelInfo gate,
rank transitions, surrender popup and Debug Spectate settings. Native font
fallbacks keep their original scale.

The large animated 3/2/1 uses the same Black face through a separate 256px
digits-only font, producing 64/128/256px assets. Initial and animated scales
use the same normalization; heading, timing and opacity semantics are retained.

The actual Geode 3.9.0 generated resources were inspected locally: all 18
bundles have the expected font sizes and valid glyph rectangles. Estimated
RGBA atlas pixels for all six fonts sum to 5.62/19.09/69.97 MiB for SD/HD/UHD.
Every atlas is below 4096px per side. These are resource estimates, not peak
iPad memory measurements. CI checks the generated artifacts against those
properties to catch stale font caches and accidental charset growth.

The host regression executes production label/fallback/width helper bodies,
tests normalized countdown scale across 60/120/144/240Hz with the existing
entry suite, and retains Korean glyph coverage. Local validation: 89 source/native callback tests, 9 strict C++ suites, 114
rules and 186 server tests, workspace typecheck/build all passed. Actual iPadOS visual and
memory verification remains pending; raster metrics can have small rounding
differences even with normalized logical scales.

Version manifest, workspace/lock metadata and exact client gate are synchronized.
Spectator protocol 2 and Debug recording format 2 are unchanged. No scoring,
pause/entry timing, native hook registrations or persistence logic is changed.
In particular, this typography patch does **not** claim to fix the reported
all-modes Clear crash or the unopened Pause menu; those remain separate work.
No manual Release workflow is run.
