# beta51 Placement and result changes

Base: authoritative `ybaf100/Corum-Ranked` main
`3412eb748c30ab4d060f6e8223fdf2929b75db98` (beta50).
Client manifest, server exact version gate and npm package versions advance
together to `v0.5.0-beta.51`.

## Placement

- Keep the existing raw weighted Placement rating and start-time policy.
  Normalize its theoretical policy range proportionally into 200–550 LP for
  three stages or 250–750 LP for five stages, then apply the existing tier
  boundary avoidance. Newly completed Placement stores **actual final LP =
  Hidden MMR**, including that boundary adjustment (600 → 584 for both).
- Migration `0017_placement_rating_policy.sql` marks existing runs
  `LEGACY_CAP_V1`. New runs explicitly use `NORMALIZED_V1`. Completed profiles
  are not rewritten; there is no MMR backfill or production DB reset here.
  The existing server startup migration runner applies the new migration.
- Show the provisional Top 2 map score beside the gameplay timer, with grade
  colors D/C/B/A/S; S uses a per-glyph rainbow. Show the server prepare deadline
  at the upper right before the map starts.
- Placement Pause adds **SKIP MAP** using the existing surrender modal. Freeze
  the current Attempt End, drain its existing idempotent FIFO through ACK, then
  submit the exact run/stage/map/level skip. Settle only that map, preserving
  its points; never skip the second map or the whole stage. Retries cannot
  target the next map. Entry-level Placement skip remains 150 LP.
- Preserve the accepted native Ranked Pause Quit lifecycle. No added native
  detours, hook priorities, protocol changes or global pause-flag writes.

## Result and profile UI

- Keep the bar at its full height even when the filled width is under 16px;
  use explicit rounded polygons with distinct edges. The track color comes
  from the immediately lower tier and wipes left to right across a tier change.
  A newly promoted BRONZE player's 607 LP is legitimately only 7/300 (2.3%)
  filled on the 600–900 range; that width is independent of the height bug.
- Track and retain exact assembled badge sprites during retirement, instead
  of probing arbitrary descendants through native RTTI. The halo is retained.
- Empty player-card status lines stay empty. `YOUR RANK` / `내 랭크` is a lobby
  fallback only; streaks, promotion and demotion warnings remain on cards.
- Show Ranked wins/losses/win rate on the first player-info row and at the
  bottom of the lobby profile, with the same localized format. Session,
  before/after result and live player projections carry authoritative counts.
  Old immutable results fall back to the live player record when counts are
  absent. A bot's Ranked record belongs to the bot; CoJong remains its GD/forum
  information reference.
- GOLD results store a compact immutable score histogram plus the moving
  opponent, captured in the same SQL snapshot as before/after standings.
  Count placed players and every historical bot, using LP competition ranks.
  No internal player IDs or MMR are exposed. Shared LP target icons use the
  existing highest-MMR selection with a stable random tie choice.
- When several higher players are passed, show and fly only the highest-ranked
  passed target, then reveal the new nearest higher player. Loss targets shrink
  and are replaced. Interpolate the bar target over 0.45s, show the current rank,
  and keep GOLD's lower bound: only LP at the GOLD floor gives zero fill, while
  a shared #1 is full. Saved older results retain their fixed-target behavior.
- The Admin player reset removes LP, MMR, tier, Placement, win/loss/streak
  stats, achievements, AP, title, history visibility and cached forum records;
  it revokes the target's sessions. Keep shared matches for the opponent and
  prevent old shared matches from backfilling erased achievements. No live
  reset action was executed during development.

## iPad demotion termination: evidence and limits

`Geode 2026-10-10 01.26.37.log` loads beta50. The last match is
`3793ad48-1566-4fc9-9f07-67bc01c7933e`:

- 01:38:49: match result and BRONZE → AQUA demotion.
- 01:38:54: final transition reveal.
- 01:38:56–57: initial BRONZE/SILVER score-overlay icons.
- 01:38:57: AQUA parts for the first score-overlay tier replacement; then an
  incomplete log line and no crash stack or exit reason.

The user reports immediate return to iPad's home screen without Geode's crash
screen. This narrows the observed phase to score-overlay tier replacement but
does **not** identify a native fault or establish memory-pressure termination.
Texture timing labels do not establish whether a cached texture was decoded.
The ownership and drawing changes above are locally verified safeguards, not
a demonstrated root cause or device-verified fix. An iPadOS GeometryJump crash
or JetsamEvent report for that exit is needed to distinguish those causes.

## Local validation

- `cd ranked && npm run typecheck && npm run test && npm run build`
  passes: rules 125 tests and server 268 tests, including Placement policy,
  recovery, skip identity/ACK, full reset, win/loss and GOLD SQL coverage.
- `node --test --test-concurrency=2 corum-ranked-mod/tests/*.mjs` plus the
  focused result-decoder rerun covers 145 passing source/native scenarios.
- Nine strict C++ executable suites pass; external asset build/validation
  tests pass (five cases). The image pack inputs are unchanged.

Native UI, renderer, GD network and lifecycle tests use boundary doubles. They
do not replace GD/iOS runtime verification. GitHub workflow runs/results and
manual Release execution are outside this handoff; only main is pushed.
