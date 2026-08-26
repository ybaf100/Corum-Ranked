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

The bundled candidate 5 set uses transparent square 1254 x 1254 PNGs. Keep
important details inside the middle 80 percent when replacing them because the
same asset is also shown at a small size on player cards and in the result
transition animation. A missing image still falls back to Geometry Dash's star
sprite and never blocks matchmaking or the Ranked UI.

## Typography

Paperlogy v1.000 is bundled in five UI roles: Regular, Medium, SemiBold,
ExtraBold, and Black. beta.6 removes the old lower-right debug glyph probe and
uses the same Paperlogy family for the selectable English/Korean Ranked UI.

Korean wording is centralized in
`corum-ranked-mod/src/ui/RankedTranslations.hpp`. Edit the Korean values there.
If an edit introduces a Hangul syllable that is not already present in that
file, add its decimal Unicode code point to the `CorumSansSemiBold`,
`CorumSansBold`, and `CorumSansBlack` charsets in `mod.json` so Geode includes
the glyph in every generated bitmap atlas.

The rejected repeating fire sprites are no longer loaded. Promotion and PERFECT
WIN states now use a restrained animated accent rail that does not cover text or
trophies.
