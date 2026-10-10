# beta53: map metadata and GOLD presentation

Base: production main `d86e2d4d2ce9bbd59ee453849d76d91ccfc400c3` (beta52).

## Behavior

- Normal maps show their GD stars inside a circle. Demons show their rounded
  GDDL Rating/DefaultRating inside a tier-colored square with white digits.
  The Integration palette is reproduced with its MIT notice in the resolver.
- Maps need only name, the actual playable ID in `대체 맵 코드`, qualifying
  percentage and tier pool. No name-based original ID lookup or native ID fallback.
  Optional legacy canonical IDs remain supported.
- Official main maps use original difficulty stars (Harder 6, Insane 8), not
  campaign reward stars. Official demons use GDDL official IDs 1/2/3; native
  official IDs are 14/18/20. The actual playable copy ID never changes.
- Length uses GDDL seconds, GD field 57 / 240, or an optional exact sheet length.
  Missing verification time shows `?m ?s`; categories/editor time are not seconds.
- Server metadata is cached in `ranked_map_metadata_cache` (migration 0018).
  Complete metadata refreshes after 24h; incomplete results retry after 5min.
  Enrichment has an 8s total network budget and 3s request limits. Failures retain
  cached data and never change playable IDs, pools, qualifying or scoring.
  A display fingerprint updates both config generations. Match/Placement views
  use their pinned catalog, so metadata refreshes do not rewrite active games.
- GOLD overtakes select the highest-ranked five passed individual players,
  including separate tied players and historical bots, and play them in sequence.
  A stalled frame cannot skip avatars; final ties do not count as overtakes.
  Skip settles the final state. Own `#n` follows the LP center between LP and bar.
- Matched SILVER/GOLD player cards use brighter ambient FX. Only the separate
  GOLD-to-SILVER demotion transition omits its background/final persistent halo.
  Score-window/lobby halos and other transitions remain available.
- Client manifest, server exact gate and package/lock versions are beta53.
  No visual image changed, so the external image pack is not repackaged.

## Local validation

- Full rules/server suite: 125 rules tests and 281 server tests passed.
- After adding pinned view metadata and generation changes: 43 focused backend
  tests passed (map metadata, match/placement flows and current version gate).
- All 147 source/native-boundary tests passed, including reruns after restoring
  three unchanged mirror test dependencies. Strict C++ executable suites: 9 passed.
- Final native decoder/handoff tests cover metadata bounds, overtake duplicates,
  maximum-five validation and halo exclusion. Cocos/GD/network are boundary doubles.
- Typecheck, rules/server build, minimal-sheet harness, image pack tests (5),
  version synchronization and diff whitespace checks passed.
- Read-only GD/GDDL endpoint samples and Geode 5.10.1 declarations were inspected.
  No full Geode platform build or device validation was performed locally.
  GitHub workflow results are not checked, as requested.

## Operator action

Replace Apps Script `RankedConfig.gs` and update the existing web-app deployment
to a new version while retaining its deployment ID/URL. This is required for
four-column sheets; a repository push does not deploy Apps Script. See
[`apps-script/README.md`](../apps-script/README.md).

## Follow-up: native compile failure

- Debug run `38064709039`, attempt 2, failed in all five native configurations
  (Windows, Android32/64, iOS Debug ON/OFF) at `MapDifficultyBadge.hpp`.
  Brace-list assignment to a polygon point was ambiguous: Geode 5.10.1
  `CCPoint` accepts assignment from both `CCPoint` and `CCSize`.
- Polygon vertices now construct `CCPoint` explicitly. The badge fixture now
  includes both assignment overloads: it reproduces the original five compile
  errors and passes with the fix. All four map-details tests pass.
- The actual SDK `CCPoint`/`CCSize` declarations, read from Geode v5.10.1,
  also compile the production polygon code with strict warnings. Circle radius
  and square corners remain unchanged. This is a type/geometry compile check,
  not a full platform build or device validation.
