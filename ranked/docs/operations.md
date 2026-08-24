# Corum Ranked 운영 가이드

## 1. 설정의 source와 snapshot

Apps Script `ranked_config`가 운영 설정의 source입니다. 서버는 마지막으로 검증에 성공한 문서만 사용하며, 새 Match 생성 시 config generation과 전체 source payload, 후보 맵을 DB에 snapshot합니다. 진행 중인 Match는 이후 Spreadsheet 변경의 영향을 받지 않습니다.

다음 값은 코드에 운영 기본값이 없습니다. 빈 값이면 config 검증이 실패하고 queue가 열리지 않습니다.

- CSMP tier별 최초 MMR Seed
- 각 표시 tier의 실제 후보 분포를 정하는 `Main Pool`
- 표시 tier의 `minInclusive` / `maxExclusive`
- placement 횟수, placement/regular K-factor, expected-score divisor, 반올림 정책
- 상대 강도 가중치 `mmrOpponentStrengthWeight`, `visibleScoreOpponentStrengthWeight` (0~2)
- session, ready, reconnect, queue/match heartbeat, orphan attempt, result 화면 timeout
- 초기/초당 확장/최대 matchmaking MMR 범위
- ready/reconnect/restart 실패 정책

고정 규칙값(300초 Round, 3-Clear, 10초 Final/LAST start window, 10초 private ban, BO3)은 명세값으로 초기 행을 만들지만 Match snapshot에 계속 포함됩니다.

## 2. Database

새 DB에는 migration을 번호 순서대로 실행합니다.

```bash
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0001_initial_ranked.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0002_debug_bot_match.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0002_attempt_start_leases.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0003_debug_rating_playable_maps.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0004_alpha37_schema_hardening.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0005_v050_rating_system.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0006_v050_dynamic_csmp_tiers.sql
```

이미 beta.2 schema가 적용된 운영 DB를 beta.3으로 올릴 때는 **서버 재배포 전에 `0006_v050_dynamic_csmp_tiers.sql`을 추가 적용**합니다. 이 변경은 기존 프로필의 Seed를 다시 적용하지 않고 저장된 최초 CSMP 티어명을 그대로 보존합니다. Migration은 transaction 안에서 실행됩니다.

beta.3에서 beta.4로 올릴 때 추가 DB migration은 없습니다. `PERFECT WIN` 판정은 기존 `ranked_rounds` 결과와 `rating_presentation` JSON을 사용합니다. 최신 `RankedConfig.gs`를 배포하고 `setupCorumRankedConfig()`를 다시 실행해 두 상대 강도 가중치 행을 추가한 뒤, 값이 0~2인지 검증합니다. 기존 입력 행은 덮어쓰지 않습니다.

`Ranked Tiers.Main Pool`은 후보 추첨의 실제 입력입니다. Main Pool 1은 Pool 1/2/3에서 2/2/1개, 2~5는 이전/본/다음 Pool에서 1/3/1개, Main Pool 6은 Pool 4/5/6에서 1/2/2개를 요구합니다. `CSMP Tiers`의 모든 이름에는 `Ranked CSMP Seed`의 대응 MMR이 필요하며 `NONE` 기준 Seed, 중복 없는 정규화 이름, 유한 숫자가 모두 확인되어야 queue가 열립니다.

## 3. 서버 환경변수

실제 값은 배포 플랫폼의 secret manager에만 저장합니다. 저장소의 `.env.example`은 placeholder뿐입니다.

- `DATABASE_URL`: PostgreSQL 연결 문자열
- `RANKED_CONFIG_URL`: 배포된 Apps Script `ranked_config` URL
- `RANKED_CONFIG_REFRESH_MS`, `RANKED_CONFIG_FETCH_TIMEOUT_MS`: Ranked config refresh/timeout 제어값
- `RANKED_CSMP_FETCH_TIMEOUT_MS`: 최초 CSMP seed source (`csmp`, `player_records`) 요청 timeout. 기본 30000ms, timeout 시 1회 재시도
- `RANKED_SESSION_TOKEN_SECRET`: production에서 최소 32자, 임의 생성값은 secret manager에서 생성
- `CORS_ORIGINS`: 필요한 origin만 쉼표로 지정
- `DISCORD_WEBHOOK_URL`: 비어 있으면 relay 비활성화
- `DISCORD_*`: Webhook을 켰을 때만 모두 명시하는 poll/batch/request/lease/retry/max-attempt 정책
- `ENABLE_DEBUG_BOT_MATCH`: 기본/production은 `false`; alpha 테스트 서버에서만 `true`
- `DEBUG_BOT_PASSWORD`, `DEBUG_BOT_*`: Bot gate와 난이도별 debug simulation 설정

서버는 production에서 HTTP Apps Script URL, 짧은 session secret, 공식 Discord 외 webhook 목적지를 거부합니다. Discord URL 자체는 로그나 message body에 넣지 않습니다.

## 4. Health와 관찰 항목

- `GET /health`: 프로세스 생존
- `GET /ready`: DB와 마지막 검증 config 상태
- config refresh 실패: 마지막 정상 snapshot이 있으면 기존 설정으로 계속 서비스하고 오류를 기록
- CSMP source timeout: action 이름과 timeout/attempt만 로그에 남기고 HTTP 503 `CSMP_SOURCE_TIMEOUT`으로 반환. player ID/secret은 로그에 남기지 않음
- outbox: `delivered_at IS NULL AND abandoned_at IS NULL`은 재시도 예정, `abandoned_at IS NOT NULL`은 최대 시도 후 포기한 알림
- Match `result_applied_at`: MMR 반영 idempotency 기준

Discord 실패는 outbox worker에서만 처리되며 Match 판정 transaction을 되돌리지 않습니다.

### 관전용 runtime state와 Start Lease

`currentAttemptProgress`는 `MatchRuntimeStatePort` 뒤의 메모리 구현에만 저장하며 DB row를 만들지 않습니다. 단일 서버 MVP에서는 이 값이 재시작 시 사라져도 점수와 판정에 영향이 없고, 살아 있는 client의 다음 telemetry로 복구됩니다. 수평 확장 시에는 같은 interface를 Redis 같은 TTL 기반 공유 저장소 구현으로 교체한 뒤 sticky routing 또는 공유 state 동작을 staging에서 검증하세요. Round가 끝나면 양쪽 임시 progress를 즉시 제거합니다.

반면 alpha.33의 `ranked_attempt_start_leases`는 점수 telemetry가 아니라 **visual attempt가 실제 start deadline 전에 시작되었다는 전송 reconciliation marker**이므로 PostgreSQL에 영속 저장합니다. 이 row 자체는 점수를 주지 않으며 authoritative `/attempt/start`가 수락되어야 실제 attempt가 생성됩니다. Render가 재시작되어도 pending lease가 남아 있어 FIFO 뒤의 정상 Start/End를 기다릴 수 있습니다.

## 5. 장애 정책

Ready timeout, reconnect timeout, restart recovery의 행동은 `Ranked Config`에서 반드시 선택합니다. 서버 시각과 `deadline_at`만 권위가 있으며 client 시각은 UI 보정용입니다. 재접속 grace가 지난 뒤 도착한 poll은 이미 확정된 forfeit/cancel 결과를 되돌리지 않습니다.

서버 재시작 전에 운영 정책과 DB backup을 확인합니다. Application bootstrap에서 진행 중 Match의 개별 config snapshot을 읽습니다. `restartRecoveryAction=CANCEL_MATCH`인 Match는 `SERVER_RESTART_RECOVERY` 사유로 취소하고, `RESUME`인 Match는 DB state/deadline을 유지해 다음 인증 poll에서 서버 시각 기준으로 상태를 전진시킵니다. `RESUME`는 staging에서 배포 중단시간과 reconnect 정책을 함께 검증하세요.

## 6. Production 전 체크리스트

- 모든 운영 미확정값 입력 및 config validation 성공
- 각 tier 후보 pool의 canonical-distinct 맵 수 충족
- Ranked와 CBF가 allowlist의 required+enabled 행인지 확인
- CBF `soft-toggle=false`, `click-on-steps=false`, `physics-bypass=false`
- staging PostgreSQL migration, two-client harness, 실제 Geode package 빌드 성공
- 일반 플레이에서 상대 progress 비공개, 3:2 LAST ATTEMPT trigger 화면에서만 관전 overlay 노출 확인
- deadline 전환이 없는 progress 요청이 PostgreSQL row/score/stateVersion을 만들지 않는지 확인
- HTTPS/TLS, reverse proxy request size/rate limit, DB backup/restore 확인
- Discord staging webhook으로 모든 7개 event type 확인
- 실제 production secret이나 webhook URL이 Git history/빌드 로그에 없는지 확인
- Debug Bot 결과도 rating에 반영되므로 staging/test 계정만 사용하고 Release에서는 server/client flag를 모두 OFF
