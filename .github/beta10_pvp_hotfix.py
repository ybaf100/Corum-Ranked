from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual < count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}: {old[:100]!r}")
    target.write_text(text.replace(old, new, count), encoding="utf-8")


path = "ranked/apps/server/src/match/match.service.ts"

replace(
    path,
    '''// PvP preparation is a real ten-second start countdown. Resource lookup, map
// acquisition, and song acquisition begin immediately inside that window; an
// old alpha-era 60-second safety floor must not extend the visible wait.
const PVP_PREPARE_SECONDS = 10;
const resourcePrepareSeconds = (_configuredReadySeconds: number): number =>
  PVP_PREPARE_SECONDS;
''',
    '''// PVP exposes a ten-second visible countdown, but the authoritative resource
// deadline is deliberately longer. Map acquisition is network-bound and Ready
// can arrive just after the visible countdown reaches zero; conflating these two
// deadlines caused beta.10 to cancel Round 1 even after the map had downloaded.
const PVP_VISIBLE_PREPARE_SECONDS = 10;
const resourcePrepareSeconds = (configuredReadySeconds: number): number =>
  Math.max(configuredReadySeconds, 60);
const visiblePrepareDeadline = (now: Date): Date =>
  new Date(now.getTime() + PVP_VISIBLE_PREPARE_SECONDS * 1_000);
''',
)

# Round 1 after bans: the match-level deadline is presentation-only (10s),
# while ranked_rounds.ready_deadline_at is the hard resource timeout (>=60s).
replace(
    path,
    '''    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET ban_a_confirmed_at = COALESCE(ban_a_confirmed_at, $2),
           ban_b_confirmed_at = COALESCE(ban_b_confirmed_at, $2),
           selected_round_maps_snapshot = $3::jsonb,
           state = 'ROUND_PREPARE', current_round_number = 1,
           deadline_at = $4, ban_deadline_at = NULL,
           state_version = state_version + 1
       WHERE id = $1 AND state = 'BAN_PHASE'`,
      [
        match.id,
        now.toISOString(),
        JSON.stringify(resolution.selectedRoundMaps),
        readyDeadline.toISOString(),
      ],
    );
    await this.insertPreparedRound(
      transaction,
      match.id,
      1,
      resolution.selectedRoundMaps[0]!,
      readyDeadline,
    );
''',
    '''    const resourceReadyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    const visibleDeadline = visiblePrepareDeadline(now);
    await transaction.query(
      `UPDATE ranked_matches
       SET ban_a_confirmed_at = COALESCE(ban_a_confirmed_at, $2),
           ban_b_confirmed_at = COALESCE(ban_b_confirmed_at, $2),
           selected_round_maps_snapshot = $3::jsonb,
           state = 'ROUND_PREPARE', current_round_number = 1,
           deadline_at = $4, ban_deadline_at = NULL,
           state_version = state_version + 1
       WHERE id = $1 AND state = 'BAN_PHASE'`,
      [
        match.id,
        now.toISOString(),
        JSON.stringify(resolution.selectedRoundMaps),
        visibleDeadline.toISOString(),
      ],
    );
    await this.insertPreparedRound(
      transaction,
      match.id,
      1,
      resolution.selectedRoundMaps[0]!,
      resourceReadyDeadline,
    );
''',
)

# Round 2/3 prepare: same split.
replace(
    path,
    '''    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await this.insertPreparedRound(transaction, match.id, roundNumber, map, readyDeadline);
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'ROUND_PREPARE', current_round_number = $2,
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, roundNumber, readyDeadline.toISOString()],
    );
''',
    '''    const resourceReadyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    const visibleDeadline = visiblePrepareDeadline(now);
    await this.insertPreparedRound(transaction, match.id, roundNumber, map, resourceReadyDeadline);
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'ROUND_PREPARE', current_round_number = $2,
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, roundNumber, visibleDeadline.toISOString()],
    );
''',
)

# Deathmatch has both fields on ranked_matches, so store both explicitly.
replace(
    path,
    '''    const readyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    await transaction.query(
      `INSERT INTO ranked_deathmatches (id, match_id, sequence, map_snapshot)
       VALUES ($1, $2, $3, $4::jsonb)`,
      [deathmatchId, match.id, sequence, JSON.stringify(map)],
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'DEATHMATCH_PREPARE', current_deathmatch_id = $2,
           ready_a_at = NULL, ready_b_at = NULL, ready_deadline_at = $3,
           deadline_at = $3, state_version = state_version + 1
       WHERE id = $1`,
      [match.id, deathmatchId, readyDeadline.toISOString()],
    );
''',
    '''    const resourceReadyDeadline = new Date(
      now.getTime() + resourcePrepareSeconds(config.operational.timeouts.readySeconds) * 1_000,
    );
    const visibleDeadline = visiblePrepareDeadline(now);
    await transaction.query(
      `INSERT INTO ranked_deathmatches (id, match_id, sequence, map_snapshot)
       VALUES ($1, $2, $3, $4::jsonb)`,
      [deathmatchId, match.id, sequence, JSON.stringify(map)],
    );
    await transaction.query(
      `UPDATE ranked_matches
       SET state = 'DEATHMATCH_PREPARE', current_deathmatch_id = $2,
           ready_a_at = NULL, ready_b_at = NULL, ready_deadline_at = $3,
           deadline_at = $4, state_version = state_version + 1
       WHERE id = $1`,
      [
        match.id,
        deathmatchId,
        resourceReadyDeadline.toISOString(),
        visibleDeadline.toISOString(),
      ],
    );
''',
)

# Regression: the public countdown remains exactly 10 seconds, but polling at
# T=0 must not resolve the resource timeout. A Ready request arriving afterward
# must still be accepted inside the hard resource window.
replace(
    "ranked/apps/server/test/match-flow.integration.test.ts",
    '''    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();

    const winRoundWithThreeClears = async (roundNumber: number) => {
''',
    '''    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();
    expect(Date.parse(state.deadlineAt as string) - clock.now().getTime()).toBe(10_000);

    // beta.10 regression: reaching the end of the visible PVP countdown is not
    // a map-download failure. The separate resource deadline must still accept
    // Ready after T=0 instead of cancelling Round 1.
    clock.advanceSeconds(10);
    state = (await matches.state(
      matchId,
      statusA.matchToken,
      playerA,
    )) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");

    const winRoundWithThreeClears = async (roundNumber: number) => {
''',
)

print("beta10 PVP timeout hotfix applied")
