# Corum Ranked Web public data patch

Based on server main `75ff093796735e5f5427bc3463b16a9d658a6cb3` and Web main
`35be47b7785a11491b9532fd4f2f95310673150b`.

The leaderboard still exposed legacy PvP placement counters, which cannot
represent a solo Run's selected length or Stage/Map progress. The Web also
aborted Google CSV requests after 15 seconds; a live response took about 30
seconds across redirects.

## Public leaderboard

`GET /api/ranked/leaderboard` now includes `placement` for Unranked players:

```json
{
  "state": "MAP_PLAYING",
  "totalStages": 3,
  "currentStageIndex": 2,
  "currentMapIndex": 1,
  "completedStages": 1,
  "completedMaps": 2
}
```

Both indices are one-based. `totalStages` comes from the saved Run's
`stage_count`, never from current config or `placementGamesRequired`.
Completed Stage/Map counts come from persisted results. `STAGE_RESULT` retains
the completed Stage index while awaiting continue/finalization. There is no
separate public `PLAY LATER` flag; it must not be inferred.

No Run: `{ "state": "NOT_STARTED" }`. Cancelled Run:
`{ "state": "CANCELLED" }`. Ranked player: `null`. If a saved Run cannot be
represented safely, the summary is `null` and the Web shows no invented progress.
Legacy `placementGames`, `placementGamesRequired` and `placementResults` remain
for compatibility, but are not Solo Stage progress or completion authority.
A finalized tier and non-null LP remain ranked even if the legacy config counter
changes. Existing tier/LP/promotion/win-rate/MMR tie-breaking order is preserved.

The single leaderboard SQL statement selects the latest Run and its result
counts. It returns no Run/Attempt IDs, private scores, MMR, Placement Rating or
session tokens. Public DB reads still work while config/matchmaking is unavailable.

## Public maps

`GET /api/ranked/maps` exposes active maps from the last validated config
snapshot. Each row contains only `title`, `difficulty`, `mapCode`
(`playableLevelId`), `pool`, and `qualifyingPercent`. The response also includes
`generation` and `fetchedAt`. It requires no game session and is independent of
matchmaking being enabled. No valid snapshot returns the existing 503 error;
the Web then falls back to the original published sheet2 CSV.

The Web tries the map API first (15-second timeout), then CSV (45-second timeout).
Google URLs remain stable. User reload/unmount cancels both paths correctly.
No DB credentials, source keys or generic third-party proxy are added to Web.

## Applying and verifying

Deploy the server patch, then Web v1.4.1. Existing Placement migrations through
`0009_placement_stage_count.sql` are required; this patch adds no migration and
changes no game scoring, Stage movement, ratings, or client version requirement.
Web deployed first remains compatible: it shows unknown Placement detail for
legacy responses and uses CSV if the map route is unavailable.

Checks performed locally:
- Web data regression tests, including delayed CSV and old-server fallback.
- Leaderboard sorting/privacy regression tests.
- Public data integration tests against PGlite with the repository migrations.
- Production TypeScript build/typecheck and Vite build.

Live Render/GitHub Pages verification is still required after these changes are
pushed/deployed. The local tests do not imply a production deployment.
