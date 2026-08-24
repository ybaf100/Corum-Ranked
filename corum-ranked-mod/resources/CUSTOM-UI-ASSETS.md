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

## Fire textures

| Filename | Used for |
| --- | --- |
| `corum-fire-promotion.png` | Promotion card particles |
| `corum-fire-perfect.png` | PERFECT WIN particles |

Recommended format: transparent PNG, 128 x 128 px. These files are particle
textures, not complete GIF animations. Use a soft white or pale-yellow core so
the game can tint the particles correctly.

Both fire textures are included with beta.5. To use a different design, replace
the corresponding PNG while keeping its filename, dimensions, and alpha channel.

If any image is absent, Corum Ranked falls back to its built-in star emblem or
the standard Geometry Dash fire particle. Missing custom images never block the
Ranked UI or matchmaking flow.
