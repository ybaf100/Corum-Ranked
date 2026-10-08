# Corum Ranked v0.4.0-alpha.3 handoff

## 이번 버전 핵심

- Debug Bot Match 결과도 일반 PvP와 동일한 `calculateMmrUpdate(ELO_V1)`를 호출해 사람 계정의 Hidden MMR, Ranked Score, tier, placement, W/L, leaderboard source, Ranked Match History에 반영합니다.
- Bot rating은 Player MMR를 기준으로 `debug-bot.config.ts`의 Easy -200 / Normal 0 / Hard +200 offset을 snapshot합니다. Bot 영구 profile은 만들지 않습니다.
- 모든 Ranked map은 `canonicalLevelId`, `alternateLevelId`, `playableLevelId`를 분리합니다. 유효한 alternate를 우선하고 없거나 유효하지 않으면 canonical로 fallback합니다.
- Round/Deathmatch 생성 후 세 ID와 Pool/Qualifying metadata가 snapshot되며 Spreadsheet 변경은 진행 중 Match에 영향을 주지 않습니다.
- Geode는 `playableLevelId`를 다운로드·실행하고 Start/Progress/End마다 같은 `levelId`를 전송합니다. 서버는 snapshot과 다르면 거부합니다.

`visible_ranked_score`는 별도 확정 변환식이 아직 없으므로 숫자 상수를 추가하지 않고, 이번 alpha에서는 rating 결과를 1:1로 기록합니다. 표시 tier의 placement 잠금은 기존 규칙대로 유지됩니다.

## 기존 alpha.2 설치에서 운영자가 할 일

1. 전체 소스를 저장소 최상위에 덮어씁니다.
2. Apps Script의 `Code.gs`와 `RankedConfig.gs`를 함께 반영하고 `setupCorumRankedConfig()`를 한 번 실행합니다. 기존 값을 덮어쓰지 않고 `Ranked Pool`의 누락된 `대체 맵 코드` header만 추가합니다.
3. `Ranked Pool`의 `Canonical Level ID`가 대표 맵 코드인지 확인합니다. 같은 canonical의 기존 Corum 맵 데이터에 대체 코드가 있으면 API가 자동으로 함께 반환하며, Ranked Pool에 명시한 유효한 대체 코드가 우선합니다.
4. 기존 alpha DB에는 다음 migration만 새로 적용합니다.

   ```bash
   psql "$DATABASE_URL" -v ON_ERROR_STOP=1 \
     -f ranked/migrations/0003_debug_rating_playable_maps.sql
   ```

5. 서버를 재배포합니다. Debug Bot을 사용할 개발 서버만 `ENABLE_DEBUG_BOT_MATCH=true`와 배포 환경의 `DEBUG_BOT_PASSWORD`를 설정합니다. secret 파일은 저장소에 추가하지 않습니다.
6. `v0.4.0-alpha.3` Debug ON Geode 빌드를 테스트 기기에 설치합니다. Bot 결과가 실제 rating에 반영되므로 테스트 서버·계정만 사용합니다.

새 DB는 `0001_initial_ranked.sql`부터 migration runner 정책에 따라 적용합니다. 이미 적용한 `0001`을 기존 DB에 재실행하지 않습니다.

## Release에서 Debug Bot 제거

- Client: `CORUM_RANKED_DEBUG_BOT_MATCH=OFF`
- Server: `ENABLE_DEBUG_BOT_MATCH=false`, `DEBUG_BOT_PASSWORD` 미배포

Client OFF 빌드는 `src/debug/*.cpp`를 source list에서 제외하고 UI/runtime guard도 전처리로 제거합니다. Production 서버는 debug flag 활성화를 시작 단계에서 거부합니다. 개발 중 쌓인 Bot rating/history를 출시 데이터에 유지할지는 출시 직전에 backup과 함께 별도로 결정합니다.

## 검증 결과

- rules: 4 files, 47 tests passed
- Ranked Server: 14 files, 36 tests passed (PGlite 직렬 최종 실행)
- PvP/Deathmatch harness: 2 files, 2 tests passed
- Apps Script Ranked config validation: passed
- 기존 Corum Integration record/alternate/evidence regression: passed
- C++ Ranked HUD/domain test: passed
- C++ Debug Bot option test: passed
- Geode 5.8.2 / GD 2.2081 Android64 Debug Bot ON package: built
- Geode 5.8.2 / GD 2.2081 Android64 Release-style Debug Bot OFF package: built; Debug UI excluded

## 이번 요구사항으로 수정한 파일

- `apps-script/RankedConfig.gs`
- `apps-script/validate-ranked-config.mjs`
- `corum-ranked-mod/src/RankedRuntime.hpp`
- `corum-ranked-mod/src/RankedRuntime.cpp`
- `corum-ranked-mod/mod.json`, `about.md`, `changelog.md`
- `ranked/packages/rules/src/types.ts`, `pool.ts`, `errors.ts`
- `ranked/packages/rules/test/fixtures.ts`, `pool-ban.test.ts`, `scoring-round.test.ts`, `match-deathmatch.test.ts`
- `ranked/apps/server/src/debug-bot/debug-bot.config.ts`, `debug-bot.types.ts`, `debug-bot.service.ts`
- `ranked/apps/server/src/match/match.dto.ts`, `match.service.ts`
- `ranked/apps/server/test/fixtures.ts`, `debug-bot-flow.integration.test.ts`, `match-flow.integration.test.ts`, `deathmatch-flow.integration.test.ts`, `migration.test.ts`, `ranked-config.service.test.ts`
- `ranked/migrations/0001_initial_ranked.sql`, `0003_debug_rating_playable_maps.sql`
- `ranked/README.md`, `ranked/docs/api.md`, `operations.md`, `debug-bot-match.md`
- workspace package/lock versions and root `README.md`
