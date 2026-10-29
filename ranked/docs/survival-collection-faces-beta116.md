# Collection face presentation (beta.116)

Collection-only square badges use `GJDifficultySprite` and its native
`updateFeatureState`: Rated = None, followed by Featured, Epic, Legendary and
Mythic. No grade name is rendered on the square. Official counts remain below
it. The difficulty overview uses a plain representative face and always ends
with the existing red-X ELIMINATED entry. Its five-slot clipped drag behavior
and policy-derived difficulty list are unchanged.

The detail order is:

```
RATED       FEATURED       EPIC
      LEGENDARY     MYTHIC
```

Every square uses the same dark Surface/242-alpha outlined chrome as the AP
plate, with only the accent following the grade. Native face/ornament bounds
are fitted together, including transformed descendants, without editing the
native canvas or individual ornament offsets. Result/history card helpers,
result layouts, official grades, persistence and collection statistics remain
unchanged.

The achievement page BADGES button now uses the lobby toolbar's .66 visual
scale, with its measured width anchored inside the right margin. The header is
fitted to the remaining space; AP, Claim and filters retain their controls.

Automated tests compile the production effect/fitting functions against renderer
boundary doubles and validate the real grid geometry. They do not verify GD
rendering or native animation/touch behavior on a physical device. Platform
builds, package publication and Singapore activation are verified separately.
