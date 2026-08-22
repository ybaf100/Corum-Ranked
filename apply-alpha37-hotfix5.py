#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

ROOT = Path.cwd()

EXPECTED_BLOBS = {
    "ranked/apps/server/src/queue/queue.service.ts": "8692686bc05dd1c26fdd7986170272a453020e51",
    "ranked/apps/server/src/match/match.service.ts": "0f1bf06b0798d962ca028931e13db8d28391063b",
    "ranked/apps/server/src/debug-bot/debug-bot.service.ts": "98da80148a605a36805183056665e9d014fce5e0",
    "corum-ranked-mod/src/LevelInfoLayer.cpp": "fd3f7909981d2a433c9f0198da0b3eac4ddb4328",
    "ranked/apps/server/test/queue.service.test.ts": "504f2d074a7343cc1e86076a898a70e30cecf3c2",
    "ranked/apps/server/test/match-flow.integration.test.ts": "8ec1a86378e5c8e0ce6f4e57b736155439cbf2ee",
    "ranked/apps/server/test/deathmatch-flow.integration.test.ts": "51ba9f0106554592ba4be9f5367722742cff070c",
    "ranked/apps/server/test/debug-bot.integration.test.ts": "4b451477fca08db6377fceaf7ba189630efa63d5",
}

ALL_MIGRATIONS = '''for (const migrationName of [
    "0001_initial_ranked.sql",
    "0002_attempt_start_leases.sql",
    "0002_debug_bot_match.sql",
    "0003_debug_rating_playable_maps.sql",
    "0004_alpha37_schema_hardening.sql",
  ])'''

OLD_MIGRATIONS = '''for (const migrationName of ["0001_initial_ranked.sql", "0002_attempt_start_leases.sql"])'''

def git_blob_sha(data: bytes) -> str:
    header = f"blob {len(data)}\0".encode()
    return hashlib.sha1(header + data).hexdigest()

def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly 1 anchor, found {count}")
    return text.replace(old, new, 1)

def load_and_verify() -> dict[str, str]:
    loaded: dict[str, str] = {}
    problems: list[str] = []
    for rel, expected in EXPECTED_BLOBS.items():
        path = ROOT / rel
        if not path.is_file():
            problems.append(f"missing: {rel}")
            continue
        data = path.read_bytes()
        actual = git_blob_sha(data)
        if actual != expected:
            problems.append(f"unexpected base: {rel}\n  expected {expected}\n  actual   {actual}")
            continue
        loaded[rel] = data.decode("utf-8")
    if problems:
        raise RuntimeError(
            "Hotfix preflight failed. This installer only applies to GitHub main "
            "at commit 9e2b92d (hotfix 4).\n" + "\n".join(problems)
        )
    return loaded

def transform(files: dict[str, str]) -> dict[str, str]:
    out = dict(files)

    p = "ranked/apps/server/src/queue/queue.service.ts"
    t = out[p]
    t = replace_once(
        t,
'''    const readyDeadline = new Date(
      now.getTime() + config.operational.timeouts.readySeconds * 1_000,
    );
    await transaction.query(
''',
'''    const readyDeadline = new Date(
      now.getTime() + config.operational.timeouts.readySeconds * 1_000,
    );

    const matchType = options.matchType ?? "RANKED_PVP";
    const debugConfig = matchType === "DEBUG_BOT" ? options.debugBotConfig ?? null : null;
    if (matchType === "DEBUG_BOT" && debugConfig === null) {
      throw new Error("Debug Bot match requires debugBotConfig");
    }
    // pg maps JavaScript null to SQL NULL. Do not JSON.stringify(null), because
    // "null"::jsonb is a JSON value and fails `debug_config IS NULL`.
    const serializedDebugConfig =
      debugConfig === null ? null : JSON.stringify(debugConfig);
    // discord_events_enabled is the original match-wide relay switch and remains
    // TRUE for public Ranked. debug_discord_events only controls Debug Bot relay.
    const debugDiscordEvents =
      matchType === "DEBUG_BOT" ? options.discordEventsEnabled ?? false : false;
    const discordEventsEnabled =
      matchType === "DEBUG_BOT" ? debugDiscordEvents : true;

    await transaction.query(
''',
        "queue createMatch derived values",
    )
    t = replace_once(
        t,
'''         $11, $11, $13, $13, $12, $14, $15::jsonb, $16,
         $15::jsonb, $16, $13
''',
'''         $11, $11, $13, $13, $12, $14, $15::jsonb, $16,
         $15::jsonb, $17, $13
''',
        "queue INSERT placeholders",
    )
    t = replace_once(
        t,
'''        options.matchType ?? "RANKED_PVP",
        JSON.stringify(options.debugBotConfig ?? null),
        options.discordEventsEnabled ?? false,
''',
'''        matchType,
        serializedDebugConfig,
        discordEventsEnabled,
        debugDiscordEvents,
''',
        "queue INSERT params",
    )
    out[p] = t

    p = "ranked/apps/server/src/match/match.service.ts"
    t = out[p]
    t = replace_once(
        t,
'''  match_type: "PVP" | "DEBUG_BOT";
  debug_bot_config: unknown | null;
  discord_events_enabled: boolean;
''',
'''  match_type: "RANKED_PVP" | "DEBUG_BOT";
  debug_config: unknown | null;
  debug_discord_events: boolean;
  // Retained compatibility column used by the existing OutboxService.
  discord_events_enabled: boolean;
''',
        "match row schema contract",
    )
    t = replace_once(
        t,
'''             server_accepted_start_at, client_started_at, level_id, client_start_event_id
           ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
''',
'''             server_accepted_start_at, client_started_at,
             level_id, played_level_id, client_start_event_id
           ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $8, $9)
''',
        "ranked attempt played_level_id",
    )
    t = replace_once(
        t,
'''         server_accepted_start_at, level_id, client_start_event_id
       ) VALUES ($1, $2, $3, $4, $5, $6, $7)`,
''',
'''         server_accepted_start_at, level_id, played_level_id, client_start_event_id
       ) VALUES ($1, $2, $3, $4, $5, $6, $6, $7)`,
''',
        "deathmatch attempt played_level_id",
    )
    out[p] = t

    p = "ranked/apps/server/src/debug-bot/debug-bot.service.ts"
    t = out[p]
    t = replace_once(t, '''  debug_bot_config: unknown;\n''', '''  debug_config: unknown;\n''', "debug bot row type")
    t = replace_once(
        t,
        '''      `SELECT m.id, m.debug_bot_config, m.player_a_id, m.player_b_id,\n''',
        '''      `SELECT m.id, m.debug_config, m.player_a_id, m.player_b_id,\n''',
        "debug bot hydrate SELECT",
    )
    t = replace_once(
        t,
        '''      const stored = parseObject(row.debug_bot_config);\n''',
        '''      const stored = parseObject(row.debug_config);\n''',
        "debug bot hydrate parse",
    )
    out[p] = t

    p = "corum-ranked-mod/src/LevelInfoLayer.cpp"
    t = out[p]
    t = replace_once(
        t,
'''        m_fields->songGateActive = true;
        m_fields->gateOpenedAt = SteadyClock::now();
''',
'''        m_fields->songGateActive = true;
        corum::ranked::RankedRuntime::get().setSongBypassAllowed(false);
        m_fields->songGateBypassed = false;
        m_fields->gateOpenedAt = SteadyClock::now();
''',
        "song bypass reset",
    )
    t = replace_once(
        t,
'''            auto const ready = songsReady(m_level, m_songWidget);
            auto const songElapsed = std::chrono::duration<double>(now - m_fields->songWaitStartedAt).count();
            if (ready) {
''',
'''            auto const ready = songsReady(m_level, m_songWidget);
            if (ready) {
''',
        "song elapsed removal",
    )
    t = replace_once(
        t,
'''            } else if (songElapsed >= 20.0) {
                // Song is optional after the existing 20-second grace period.
                // The vanilla song widget remains alive, so a background download
                // can continue while Ranked starts without audio.
                runtime.setSongBypassAllowed(true);
                m_fields->songGateBypassed = true;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG TIMEOUT - STARTING WITHOUT SONG");
                    m_fields->gateStatus->setColor({255, 216, 86});
                }
''',
'''            } else if (now >= m_fields->countdownEndsAt) {
                // The visible Ranked countdown is the resource wait budget for
                // optional music. A broken/slow custom-song API must never keep a
                // fully downloaded playable map stuck after STARTS IN 00.
                // The vanilla widget remains alive, so audio may still finish
                // downloading while gameplay proceeds without it.
                runtime.setSongBypassAllowed(true);
                m_fields->songGateBypassed = true;
                if (m_fields->gateStatus) {
                    m_fields->gateStatus->setString("SONG NOT READY - STARTING WITHOUT SONG");
                    m_fields->gateStatus->setColor({255, 216, 86});
                }
''',
        "song countdown bypass",
    )
    out[p] = t

    for p in [
        "ranked/apps/server/test/queue.service.test.ts",
        "ranked/apps/server/test/match-flow.integration.test.ts",
        "ranked/apps/server/test/deathmatch-flow.integration.test.ts",
        "ranked/apps/server/test/debug-bot.integration.test.ts",
    ]:
        out[p] = replace_once(out[p], OLD_MIGRATIONS, ALL_MIGRATIONS, f"{p} migrations")

    p = "ranked/apps/server/test/queue.service.test.ts"
    t = out[p]
    t = replace_once(
        t,
'''    expect(match.rows[0]!.candidate_maps_snapshot).toHaveLength(5);
    const snapshots = await database.query<{ count: number }>(
''',
'''    expect(match.rows[0]!.candidate_maps_snapshot).toHaveLength(5);

    const schemaShape = await database.query<{
      match_type: string;
      debug_bot_config: unknown | null;
      discord_events_enabled: boolean;
      debug_config: unknown | null;
      debug_discord_events: boolean;
    }>(
      `SELECT match_type, debug_bot_config, discord_events_enabled,
              debug_config, debug_discord_events
       FROM ranked_matches`,
    );
    expect(schemaShape.rows[0]).toMatchObject({
      match_type: "RANKED_PVP",
      debug_bot_config: null,
      discord_events_enabled: true,
      debug_config: null,
      debug_discord_events: false,
    });

    const snapshots = await database.query<{ count: number }>(
''',
        "queue schema regression assertion",
    )
    out[p] = t

    return out

def validate_transformed(files: dict[str, str]) -> None:
    checks = {
        "ranked/apps/server/src/queue/queue.service.ts": [
            'const matchType = options.matchType ?? "RANKED_PVP";',
            'debugConfig === null ? null : JSON.stringify(debugConfig);',
            'const discordEventsEnabled =',
            '$15::jsonb, $17, $13',
        ],
        "ranked/apps/server/src/match/match.service.ts": [
            'match_type: "RANKED_PVP" | "DEBUG_BOT";',
            'level_id, played_level_id, client_start_event_id',
        ],
        "ranked/apps/server/src/debug-bot/debug-bot.service.ts": [
            'debug_config: unknown;',
            'SELECT m.id, m.debug_config',
            'parseObject(row.debug_config)',
        ],
        "corum-ranked-mod/src/LevelInfoLayer.cpp": [
            'setSongBypassAllowed(false);',
            'now >= m_fields->countdownEndsAt',
            'SONG NOT READY - STARTING WITHOUT SONG',
        ],
        "ranked/apps/server/test/queue.service.test.ts": [
            '"0004_alpha37_schema_hardening.sql"',
            'match_type: "RANKED_PVP"',
            'debug_config: null',
        ],
    }
    for rel, markers in checks.items():
        text = files[rel]
        for marker in markers:
            if marker not in text:
                raise RuntimeError(f"post-transform validation failed: {rel}: {marker!r}")

def main() -> int:
    check_only = "--check" in sys.argv
    try:
        current = load_and_verify()
        patched = transform(current)
        validate_transformed(patched)
    except Exception as exc:
        print(f"[FAIL] {exc}", file=sys.stderr)
        return 1

    if check_only:
        print("[OK] exact hotfix-4 base verified; all hotfix transformations are applicable.")
        return 0

    for rel, text in patched.items():
        path = ROOT / rel
        tmp = path.with_name(path.name + ".alpha37-hotfix5.tmp")
        tmp.write_text(text, encoding="utf-8", newline="\n")
        tmp.replace(path)

    print("[OK] Corum Ranked v0.4.0-alpha.37 hotfix 5 applied.")
    print("[OK] Changed files:")
    for rel in patched:
        print(f"  - {rel}")
    print()
    print("Recommended verification:")
    print("  cd ranked")
    print("  npm test")
    print("  npm run build")
    print()
    print("Then rebuild the Geode mod because corum-ranked-mod/src/LevelInfoLayer.cpp changed.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
