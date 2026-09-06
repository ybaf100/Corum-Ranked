from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


service_path = Path("ranked/apps/server/src/match/match.service.ts")
service = service_path.read_text()

service = replace_once(
    service,
    '''  AttemptProgressDto,\n  AttemptStartDto,\n  AttemptStartIntentDto,\n  ReadyMatchDto,''',
    '''  AttemptProgressDto,\n  AttemptStartDto,\n  AttemptStartIntentDto,\n  PracticePresenceDto,\n  ReadyMatchDto,''',
    "service import PracticePresenceDto",
)

service = replace_once(
    service,
    '''    return this.state(matchId, matchToken, session);\n  }\n\n  public async startAttemptIntent(''',
    '''    return this.state(matchId, matchToken, session);\n  }\n\n  public async updatePracticePresence(\n    matchId: string,\n    matchToken: string,\n    session: RankedSessionContext,\n    body: PracticePresenceDto,\n  ) {\n    const now = this.clock.now();\n    return this.database.transaction(async (transaction) => {\n      const authorization = await this.access.authorize(transaction, matchId, session, matchToken);\n      let match = await this.lockMatch(transaction, matchId);\n      match = await this.advanceLockedMatch(transaction, match, now);\n\n      let runtimeSlot: number | null = null;\n      if (match.state === "DEATHMATCH_PLAYING" && match.current_deathmatch_id) {\n        const deathmatch = await this.currentDeathmatch(transaction, match);\n        runtimeSlot = deathmatchRuntimeSlot(Number(deathmatch.sequence));\n      } else if (this.isRoundPlaying(match.state) && match.current_round_number) {\n        runtimeSlot = Number(match.current_round_number);\n      }\n\n      if (runtimeSlot === null) {\n        return {\n          accepted: false,\n          practice: false,\n          reason: `PRACTICE_PRESENCE_NOT_ACCEPTED_IN_${match.state}`,\n          serverNow: now.toISOString(),\n        };\n      }\n\n      // Practice presence is deliberately non-scoring and ephemeral. It exists\n      // only so the opponent HUD can show the wrench; no Attempt/clock/round\n      // state is mutated here.\n      this.runtimeState.setPractice(match.id, runtimeSlot, authorization.side, body.practice);\n      return {\n        accepted: true,\n        practice: body.practice,\n        reason: null,\n        serverNow: now.toISOString(),\n      };\n    });\n  }\n\n  public async startAttemptIntent(''',
    "service updatePracticePresence",
)

service = replace_once(
    service,
    '''    const map = deathmatchMap;\n    const awardedScore = scoreAttempt(\n      body.progressPercent,\n      body.cleared,\n      map.qualifyingPercent,\n    );''',
    '''    const map = deathmatchMap;\n    // Death Match has no Qualifying threshold. Persist the raw authoritative\n    // percentage in the compatibility awarded_score column.\n    const awardedScore = body.cleared ? 100 : body.progressPercent;''',
    "deathmatch raw awarded score",
)

service = replace_once(
    service,
    '''           COALESCE(SUM(awarded_score) FILTER (WHERE player_id = $2 AND valid), 0) AS score_a,\n           COALESCE(SUM(awarded_score) FILTER (WHERE player_id = $3 AND valid), 0) AS score_b,''',
    '''           COALESCE(MAX(awarded_score) FILTER (WHERE player_id = $2 AND valid), 0) AS score_a,\n           COALESCE(MAX(awarded_score) FILTER (WHERE player_id = $3 AND valid), 0) AS score_b,''',
    "deathmatch clear event best score",
)

service = replace_once(
    service,
    '''    const committedScore = (rows: DeathmatchAttemptRow[]) => rows.reduce(\n      (total, attempt) => total + (attempt.valid && attempt.ended_at !== null ? Number(attempt.awarded_score) : 0),\n      0,\n    );''',
    '''    const committedBestPercent = (rows: DeathmatchAttemptRow[]) => Math.max(\n      0,\n      ...rows\n        .filter((attempt) => attempt.valid && attempt.ended_at !== null)\n        .map((attempt) => Boolean(attempt.cleared) ? 100 : Number(attempt.progress_percent ?? 0)),\n    );''',
    "deathmatch committed best percent helper",
)

service = replace_once(
    service,
    '''    const scoreA = committedScore(rowsA);\n    const scoreB = committedScore(rowsB);''',
    '''    const scoreA = committedBestPercent(rowsA);\n    const scoreB = committedBestPercent(rowsB);''',
    "deathmatch committed values",
)

service = replace_once(
    service,
    '''      displayScores: {\n        A: scoreA + (progressA && activeA\n          ? scoreAttempt(progressA.progressPercent, false, map.qualifyingPercent)\n          : 0),\n        B: scoreB + (progressB && activeB\n          ? scoreAttempt(progressB.progressPercent, false, map.qualifyingPercent)\n          : 0),\n      },''',
    '''      displayScores: {\n        A: Math.max(scoreA, progressA && activeA ? progressA.progressPercent : 0),\n        B: Math.max(scoreB, progressB && activeB ? progressB.progressPercent : 0),\n      },''',
    "deathmatch live best display",
)

service = replace_once(
    service,
    '''      attemptsRemaining: { A: Math.max(0, 3 - rowsA.length), B: Math.max(0, 3 - rowsB.length) },''',
    '''      // Remaining budget changes only after authoritative End.\n      attemptsRemaining: { A: Math.max(0, 3 - completed(rowsA)), B: Math.max(0, 3 - completed(rowsB)) },''',
    "deathmatch remaining after end",
)

service = replace_once(
    service,
    '''    const map = parseJson<RankedMapSnapshot>(deathmatch.map_snapshot);\n    const evaluation = evaluateDeathmatch(\n      map,\n      attemptsA.map((attempt) => ({''',
    '''    const evaluation = evaluateDeathmatch(\n      attemptsA.map((attempt) => ({''',
    "evaluateDeathmatch signature",
)

service = replace_once(
    service,
    '''    let currentRound: object | null = null;\n    let opponentProgress: number | null = null;\n    let spectator: {''',
    '''    let currentRound: object | null = null;\n    let opponentProgress: number | null = null;\n    let opponentFinished = false;\n    let opponentPractice = false;\n    let spectator: {''',
    "public state opponent flags",
)

service = replace_once(
    service,
    '''        opponentProgress = this.runtimeState.displayProgress(\n          match.id,\n          round.round_number,\n          opponentSide,\n        );\n        const qualifyingPercent = Number(round.qualifying_percent);''',
    '''        opponentProgress = this.runtimeState.displayProgress(\n          match.id,\n          round.round_number,\n          opponentSide,\n        );\n        opponentPractice = this.runtimeState.practice(match.id, round.round_number, opponentSide);\n        if (domain) {\n          const opponentAttempts = domain.attempts[opponentSide];\n          const opponentActive = opponentAttempts.some(\n            (attempt) => attempt.valid && attempt.endedAtMs === null,\n          );\n          const opponentHasEnded = opponentAttempts.some(\n            (attempt) => attempt.valid && attempt.endedAtMs !== null,\n          );\n          opponentFinished = this.runtimeState.finished(match.id, round.round_number, opponentSide) ||\n            (!opponentActive && opponentHasEnded);\n        }\n        const qualifyingPercent = Number(round.qualifying_percent);''',
    "round opponent lifecycle",
)

service = replace_once(
    service,
    '''      opponentProgress = this.runtimeState.displayProgress(\n        match.id,\n        deathmatchRuntimeSlot(Number(current.sequence)),\n        opponentSide,\n      );\n      const result = await transaction.query<{ winner_id: string | null }>(''',
    '''      const deathmatchSlot = deathmatchRuntimeSlot(Number(current.sequence));\n      opponentProgress = this.runtimeState.displayProgress(\n        match.id,\n        deathmatchSlot,\n        opponentSide,\n      );\n      opponentPractice = this.runtimeState.practice(match.id, deathmatchSlot, opponentSide);\n      const liveRecord = live as {\n        attemptsCompleted: Record<PlayerSide, number>;\n        activeAttemptNumber: Record<PlayerSide, number | null>;\n      };\n      opponentFinished = liveRecord.attemptsCompleted[opponentSide] > 0 &&\n        liveRecord.activeAttemptNumber[opponentSide] === null;\n      const result = await transaction.query<{ winner_id: string | null }>(''',
    "deathmatch opponent lifecycle",
)

service = replace_once(
    service,
    '''      side: viewerSide,\n      opponentProgress,\n      ready,''',
    '''      side: viewerSide,\n      opponentProgress,\n      opponentFinished,\n      opponentPractice,\n      ready,''',
    "public state expose opponent lifecycle",
)

service_path.write_text(service)


test_path = Path("ranked/apps/server/test/deathmatch-flow.integration.test.ts")
test = test_path.read_text()

test = replace_once(
    test,
    '''import {\n  createMatchSeries,\n  scoreAttempt,\n  type CsmpTier,''',
    '''import {\n  createMatchSeries,\n  type CsmpTier,''',
    "integration remove scoreAttempt import",
)

test = replace_once(
    test,
    '''      const levelId = playing.deathmatch.map.playableLevelId as string;\n      const qualifying = Number(playing.deathmatch.map.qualifyingPercent);''',
    '''      const levelId = playing.deathmatch.map.playableLevelId as string;''',
    "integration remove qualifying",
)

test = replace_once(
    test,
    '''            expect(\n              live.deathmatchSnapshot.displayScores[context === contextA ? "A" : "B"],\n            ).toBe(scoreAttempt(progress, false, qualifying));''',
    '''            expect(\n              live.deathmatchSnapshot.displayScores[context === contextA ? "A" : "B"],\n            ).toBe(progress);''',
    "integration live raw percent",
)

test = replace_once(
    test,
    '''            expect(opponentView.opponentProgress).toBe(progress);''',
    '''            expect(opponentView.opponentProgress).toBe(progress);\n            expect(opponentView.opponentFinished).toBe(false);''',
    "integration active not finished",
)

test = replace_once(
    test,
    '''          if (context === contextB && attempt === 3) {\n            // The final End settles the Deathmatch and clears its runtime slot;\n            // no stale percentage is exposed in the result state.\n            expect(opponentViewAfterEnd.opponentProgress).toBeNull();\n          } else {\n            expect(opponentViewAfterEnd.opponentProgress).toBe(0);\n          }''',
    '''          expect(opponentViewAfterEnd.opponentFinished).toBe(true);''',
    "integration finished after end",
)

test = replace_once(
    test,
    '''    let deathState = await playDeathmatch(70, 70);''',
    '''    // Practice presence is viewer-relative UI state only and consumes no Attempt.\n    await service.ready(matchId, matchTokenA, contextA, { installedMods: structuredClone(cleanMods) });\n    await service.ready(matchId, matchTokenB, contextB, { installedMods: structuredClone(cleanMods) });\n    await service.updatePracticePresence(matchId, matchTokenA, contextA, { practice: true });\n    let presenceView = (await service.state(matchId, matchTokenB, contextB)) as Record<string, any>;\n    expect(presenceView.opponentPractice).toBe(true);\n    expect(presenceView.deathmatch.attemptsUsed.A).toBe(0);\n    await service.updatePracticePresence(matchId, matchTokenA, contextA, { practice: false });\n    presenceView = (await service.state(matchId, matchTokenB, contextB)) as Record<string, any>;\n    expect(presenceView.opponentPractice).toBe(false);\n\n    let deathState = await playDeathmatch(70, 70);''',
    "integration practice presence",
)

test = replace_once(
    test,
    '''    expect(deathmatches.rows[1]!.winner_id).toBe(playerAId);''',
    '''    expect(deathmatches.rows[1]!.winner_id).toBe(playerAId);\n    expect(Number(deathmatches.rows[1]!.score_a)).toBe(80);\n    expect(Number(deathmatches.rows[1]!.score_b)).toBe(70);''',
    "integration persisted best percent",
)

test_path.write_text(test)
