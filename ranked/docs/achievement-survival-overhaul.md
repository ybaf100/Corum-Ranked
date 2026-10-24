# Survival achievement overhaul (released as beta.95)

Base: remote main `979ce1ee80256d1ca867c245e264d8049f605d98`. The catalog has **48 achievements**: Solo Ranked 10 progressive + 10 one-shot, Survival 7 + 12, Shared 4 + 5.

## Authority and counting

- Shared Trailblazer uses completed, started Solo rounds (including completed Death Matches) with a valid ended attempt by that player, plus played Survival rounds with a valid entered, ended attempt. It counts rounds, not attempts or qualifications.
- Shared Unstoppable adds official Solo clears to the number of valid Survival rounds with at least one accepted 100% attempt; repeated 100% attempts in a round add only one.
- Round Ruler counts any official rank 1, including tied first. Clean Champion separately requires sole rank 1 in every valid round of the winning match.
- Underdog requires official rank **3** in the qualifying round immediately before the first final. A lowest seed of 2 or 4 does not qualify; tied third retains official rank 3.
- Death Match entry is a tied FINAL advancing into TIEBREAK. Titans Clash requires a tied already-running TIEBREAK advancing into another TIEBREAK. Tiebreak comparison uses the existing best-of-three milli-percent rules. Normal-round scoring, cutoff ties, 180-second timers, LP and matchmaking are unchanged.
- Best/current win and podium streaks are recomputed from immutable per-match facts ordered by completion time and match ID, with duplicate match IDs ignored. The removed progressive win-streak card does not remove its statistics: five consecutive wins unlock True Champion once.
- Debug real humans retain the existing server-approved result policy; bots, observers, Practice and local replays receive no player achievements.

## Last ten seconds: durable live evidence

The match-row lock serializes the server recovery tick, human commands and bot adapters. Before any post-boundary mutation, the server records `achievementCheckpoint` in the match snapshot and `sur_rounds.achievement_checkpoint` in the same transaction. The ordinary 250 ms recovery loop now observes elimination rounds at `endsAt - 10000`, without requiring a client poll.

The logical boundary includes previously accepted state and precedes commands accepted at that boundary. One recovery tick (250 ms) of scheduler slack is permitted **only when the persisted snapshot has not changed after the boundary**. Thus later scores never enter the boundary standings. If the server observes later than that, restarts too late, or sees an already-mutated post-boundary snapshot, it persists `MISSED` and awards nothing. It never reconstructs danger from a completed round's attempt history.

The evidence records the official integer scores/ranks and cutoff-tie rule. Only a player strictly below the passing cutoff is at risk. Accepted live state must then put that player in the passing group before the deadline (`rescuedAt`), and the official final round result must actually qualify them. A transient rescue followed by elimination does not unlock it. No HTTP DTO accepts this evidence. Retries retain the original checkpoint and first recovery timestamp.

## Hidden presentation and titles

The public achievement state exposes opaque secret IDs, HIDDEN, rarity/AP and only the localized hint before unlock. It omits the condition and true name/description. Native presentation also selects the hint key instead of the real bundled translation while concealed, then reveals ordinary copy after unlock. Claim/AP ledger uniqueness and server title ownership checks remain in force.

The existing server scope prefixes are retained. The native equipped-title path previously replaced a prefixed server name with an unprefixed translation; it now retains scope and formats it consistently in profiles, equipped titles, selectors, cards and both modes' unlock notices, without storing a prefix in the ID.

Original V1 diamond `#8DEBFF`, lower-tier colors, animated seven-pastel V/Mythic progress fills and static Claim All controls remain unchanged. The API's stage V display color is aligned with the native original diamond.

## One-time reset and deployment

**Production rollout completed 2026-10-24; see [verified rollout](achievement-beta95-production-rollout.md).** The new `0029_achievement_survival_overhaul.sql` migration runs in one transaction and guards its reset with `ranked_achievement_versions.overhaul_activated_at`. A failed migration rolls back; re-executing after success or an interrupted migration-ledger write does not reset earned new data. Existing migrations are not edited.

Only achievement metrics, unlocks, events, AP ledger/total, equipped achievement titles, Survival achievement facts, checkpoint evidence and cached achievement toasts are cleared. Accounts/authentication, Solo and Survival LP/MMR/rank, match/round history, actual attempts and server/matching configuration are preserved. Non-achievement rating/result payload fields remain intact.

Lazy recovery and direct settlement reject source matches/runs created before the new epoch, so clearing a cache cannot re-unlock old history; in-flight pre-cutover matches also do not count. New matches after the epoch remain recoverable. `0020_deathmatch_native_death.sql` is added to the required migration list, with readiness probes for `native_death` and all new fields.

The initial implementation was staged on main with **`[no-deploy] [build only]`**. The Lightsail workflow explicitly skips such a push even if `LIGHTSAIL_AUTO_DEPLOY=true`; existing manual deployment, explicit deploy markers, pinned SSH, Singapore host check, concurrency and rollback are preserved. This commit does not publish/overwrite the immutable beta.94 packages. The subsequent coordinated release synchronized package metadata to beta.95, published both channels, and activated Singapore.

Before a later production rollout: preserve a verified backup/restore path, stop the old writer before migration, build a new synchronized client version, verify both immutable R2 channels before activating its exact server version, then deliberately deploy to Singapore. That later startup will execute the authorized achievement-only reset. Never deploy to retired Seoul or run the Railway bridge as a game writer. The epoch reset cannot be undone by a code-only rollback; restoring achievement data requires the prior backup.

## Final catalog

All progressive stages award 5 / 10 / 20 / 40 / 75 AP. Survival maximum is **1,050 + 895 = 1,945 AP**. Hidden conditions below are maintainer documentation and are not returned in locked public state.

### SOLO_RANKED

| ID | 한국어 / English | 기준 또는 조건 | 단계 I–V / 희귀도 · AP |
|---|---|---|---|
| `ranked_wins` | 승리자 / Winner | Solo Ranked에서 승리하세요. | 1 / 10 / 50 / 100 / 500 |
| `ranked_matches` | 경쟁자 / Competitor | Solo Ranked에서 경기를 완료하세요. | 10 / 50 / 100 / 500 / 1000 |
| `ranked_clears` | 클리어러 / Clearer | Solo Ranked에서 맵을 Clear하세요. | 1 / 10 / 100 / 500 / 1000 |
| `perfect_wins` | 완벽주의자 / Perfectionist | Solo Ranked에서 PERFECT WIN을 달성하세요. | 1 / 5 / 10 / 25 / 50 |
| `win_streak` | 연승 / Win Streak | Solo Ranked에서 최고 연승 기록을 높이세요. | 3 / 5 / 10 / 15 / 20 |
| `deathmatch_wins` | 결투사 / Duelist | Solo Ranked에서 Death Match에서 승리하세요. | 1 / 5 / 10 / 25 / 50 |
| `last_attempt_clears` | 마지막 기회 / Last Chance | Solo Ranked에서 LAST ATTEMPT에서 Clear하세요. | 1 / 3 / 10 / 25 / 50 |
| `reverse_sweeps` | 역전승 / Reverse Sweep | Solo Ranked에서 0:1에서 2:1로 역전승하세요. | 1 / 3 / 10 / 25 / 50 |
| `sweep_wins` | 완승 / Sweep | Solo Ranked에서 경기를 2:0으로 승리하세요. | 1 / 5 / 20 / 50 / 100 |
| `promotion_wins` | 승격 / Promotion | Solo Ranked에서 Promotion에 성공하세요. | 1 / 2 / 3 / 5 / 10 |
| `reverse_sweep_first` | 뒤집었다 / Turned It Around | Solo Ranked에서 0:1에서 2:1로 첫 역전승을 달성하세요. | RARE · 25 |
| `last_attempt_match_win` | 아직 끝나지 않았다 / It Is Not Over | Solo Ranked에서 LAST ATTEMPT에서 Clear한 뒤 Match에서 승리하세요. | RARE · 25 |
| `deathmatch_tie` | 다시 한 번 / Once More | Solo Ranked에서 Death Match 동점으로 새 맵을 맞이하세요. | RARE · 25 |
| `deathmatch_close_win` | 간발의 차 / By a Hair | Solo Ranked에서 1%p 미만 차이로 Death Match에서 승리하세요. | RARE · 25 |
| `deathmatch_pixel_win` | 한 끗 차이 / Pixel Perfect | Solo Ranked에서 0.1%p 미만 차이로 Death Match에서 승리하세요. | EPIC · 50 |
| `deathmatch_domination` | 압도적 / Dominating | Solo Ranked에서 50%p 이상 차이로 Death Match에서 승리하세요. | RARE · 25 |
| `deathmatch_hundred_win` | 완벽한 결투 / Perfect Duel | Solo Ranked에서 Death Match에서 100%를 기록하고 승리하세요. | EPIC · 50 |
| `deathmatch_double_hundred` | 양보는 없다 / No Concessions | Solo Ranked에서 양쪽 모두 100%로 Death Match에서 비기세요. | MYTHIC · 200 |
| `perfect_win_streak_3` | 무결점 / Flawless | Solo Ranked에서 PERFECT WIN을 3경기 연속 달성하세요. | LEGENDARY · 100 |
| `placement_complete` | 첫 걸음 / First Step | Solo Ranked에서 정상 Placement를 완료하세요. | COMMON · 10 |

### SURVIVAL

| ID | 한국어 / English | 기준 또는 조건 | 단계 I–V / 희귀도 · AP |
|---|---|---|---|
| `survival_matches` | 서바이버 / Survivor | 서바이벌 경기를 정상 완료하세요. | 5 / 25 / 100 / 250 / 500 |
| `survival_wins` | 우승 / Champion | 서바이벌에서 최종 우승하세요. | 1 / 3 / 10 / 25 / 50 |
| `survival_podiums` | 포디움 / Podium | 서바이벌 최종 Top 3에 입상하세요. | 1 / 5 / 20 / 50 / 100 |
| `survival_finals` | 파이널리스트 / Finalist | 서바이벌 결승에 진출하세요. | 1 / 5 / 20 / 50 / 100 |
| `survival_round_survives` | 불사조 / Phoenix | 서바이벌 라운드를 통과하세요. | 10 / 50 / 200 / 500 / 1000 |
| `survival_round_wins` | 라운드 지배자 / Round Ruler | 서바이벌 라운드에서 단독 또는 공동 1위를 달성하세요. | 1 / 10 / 25 / 50 / 100 |
| `survival_podium_streak` | 마스터 / Master | 최고 연속 Top 3 입상 기록을 높이세요. | 2 / 3 / 5 / 8 / 12 |
| `survival_first_match` | 서바이벌 데뷔 / Survival Debut | 첫 서바이벌 경기를 완료하세요. | COMMON · 10 |
| `survival_first_podium` | 첫 입상 / First Podium | 서바이벌에서 처음으로 Top 3에 입상하세요. | RARE · 25 |
| `survival_first_win` | 첫 우승 / First Champion | 서바이벌에서 처음으로 우승하세요. | EPIC · 50 |
| `survival_cutoff_escape` | 턱걸이 생존 / Just Made It | 정확히 컷오프 경계 순위로 통과하세요. | COMMON · 10 |
| `survival_round_perfect` | 완벽한 생존 / Perfect Survivor | 유효한 서바이벌 라운드에서 100%를 기록하세요. | RARE · 25 |
| `survival_clean_champion` | 완전 우승 / Clean Champion | 모든 라운드에서 단독 1위로 우승하세요. | LEGENDARY · 100 |
| `survival_final_perfect_win` | 완벽한 피날레 / Perfect Finale | 결승에서 100%를 기록하고 최종 우승하세요. | LEGENDARY · 100 |
| `survival_underdog` | 언더독 / Underdog | 3등으로 결승에 진출한 뒤 최종 우승하세요. | EPIC · 50 |
| `survival_deathmatch_entry` | 데스매치 / Death Match | 서바이벌 결승에서 동점으로 데스매치에 진입하세요. | EPIC · 50 |
| `survival_secret_1` | 아직 끝나지 않았다 / It Is Not Over (Hidden) | 종료 10초 전 탈락 예정 상태에서 벗어나 라운드를 통과하세요. | EPIC · 75 |
| `survival_true_champion` | 진정한 1등 / True Champion | 서바이벌에서 5경기 연속 우승하세요. | MYTHIC · 200 |
| `survival_secret_2` | 자강두천 / Titans Clash (Hidden) | 진행 중인 서바이벌 데스매치에서 동점을 기록하세요. | MYTHIC · 200 |

### SHARED

| ID | 한국어 / English | 기준 또는 조건 | 단계 I–V / 희귀도 · AP |
|---|---|---|---|
| `shared_matches` | 개척자 / Pioneer | 두 모드의 완료 경기 수를 높이세요. | 20 / 100 / 250 / 500 / 1000 |
| `shared_wins` | 정복자 / Conqueror | 솔랭 승리와 서바이벌 우승을 기록하세요. | 5 / 25 / 100 / 250 / 500 |
| `shared_mastery` | 돌파자 / Trailblazer | 정상적으로 진행된 솔랭과 서바이벌 라운드를 기록하세요. | 25 / 100 / 300 / 700 / 1500 |
| `shared_clears` | 파죽지세 / Unstoppable | 솔랭 맵 클리어와 서바이벌 100% 달성 라운드를 기록하세요. | 25 / 100 / 300 / 700 / 1500 |
| `gold_reached` | 정상 / The Summit | Solo Ranked 또는 Survival에서 GOLD 티어에 처음 도달하세요. | EPIC · 50 |
| `dual_mode_debut` | 두 세계의 시작 / Two Worlds Begin | 두 모드에서 각각 한 경기를 완료하세요. | COMMON · 10 |
| `dual_mode_winner` | 두 세계의 승자 / Winner of Two Worlds | 솔랭 1승과 서바이벌 1우승을 기록하세요. | RARE · 25 |
| `dual_gold_reached` | 양쪽의 정상 / Twin Summits | 정상 경기로 두 모드에서 모두 GOLD에 도달하세요. | LEGENDARY · 100 |
| `master_of_two_worlds` | 두 세계의 지배자 / Master of Two Worlds | 두 모드에서 각각 성장형 V를 달성하세요. | MYTHIC · 200 |

## Validation

Local results and platform-build limitations are recorded in the pull request and final handoff. GitHub Actions results are intentionally not waited for or checked, as requested by the owner. Local native tests compile actual source/domain code with lifecycle doubles on Linux; they do not prove a packaged iOS/Android/Windows build or physical-device behavior.

Verified local commands/results:

| Check | Result |
|---|---|
| `npm ci --prefix ranked` | Pinned dependencies installed |
| `npm run typecheck --prefix ranked` | Rules and server, including test sources: pass |
| `npm run build --prefix ranked` | Rules and server TypeScript build: pass |
| Rules Vitest | 18 files, 282 tests: pass |
| Server Vitest | 59 files, 584 tests: pass |
| PostgreSQL 16.14 on localhost only | Online HTTP/concurrency/recovery suite: 48 passed; added concurrent checkpoint/reconnect/DTO privacy case: 1 passed |
| `node --test corum-ranked-mod/tests/*.mjs` | 275 unique tests verified: 271 initially passed; four missing-SDK-dependency cases passed on rerun after preparing the exact JSON/Result revisions from the repository workflow |
| `node ranked/tools/localization/build.mjs --check` | 622 bilingual keys, 48 achievements: pass |
| Survival simulator | 1,050 NORMAL matches and ten virtual hours: pass |
| `git diff --check` | Pass |
| GitHub Actions / packaged Windows, iOS, Android32/64 builds | Not checked or waited for, per owner request |
| Production Neon migration / Lightsail rollout / immutable R2 publication | Not performed |
| Physical Geode/iPadOS/Android/Windows behavior | Not tested |

The SQL tests cover injected reset failure/rollback, one-time reruns, old-history recovery blocking, complete unrelated-table preservation, retained real Solo and Survival attempts/results/LP, concurrent Claim/ledger uniqueness and five-win/best-streak recovery in completion order. Native tests exercise the actual Hidden presentation helpers and scope prefixes in both languages; existing actual-source lifecycle/transport/animation regressions also pass.
