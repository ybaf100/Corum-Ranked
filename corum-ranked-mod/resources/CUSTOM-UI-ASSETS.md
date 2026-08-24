# Corum Ranked custom UI assets

Place replacement images directly in this directory before building the mod.
Keep the filenames exactly as listed below.

## Rank trophies

| Filename | Used for |
| --- | --- |
| `corum-trophy-unranked.png` | Unranked and fallback profile |
| `corum-trophy-red.png` | RED tier |
| `corum-trophy-aqua.png` | AQUA tier |
| `corum-trophy-bronze.png` | BRONZE tier |
| `corum-trophy-silver.png` | SILVER tier |
| `corum-trophy-gold.png` | GOLD tier |

Recommended format: transparent PNG, square canvas, 512 x 512 px. Keep important
details inside the middle 80 percent because the same asset is also shown at a
small size on player cards.

The trophy images are intentionally not bundled yet. Until they are supplied,
Corum Ranked uses a smaller built-in Geometry Dash star tinted to the tier color.
Missing custom images never block the Ranked UI or matchmaking flow.

## Typography

Paperlogy v1.000 is bundled in four UI roles: Regular, Medium, SemiBold, and
ExtraBold. The debug build includes the Korean text `디버그` in the lower-right
corner of the Ranked lobby as an explicit bitmap-atlas glyph test.

The rejected repeating fire sprites are no longer loaded. Promotion and PERFECT
WIN states now use a restrained animated accent rail that does not cover text or
trophies.
