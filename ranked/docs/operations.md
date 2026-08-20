# Corum Ranked 운영 가이드

## 1. 설정의 source와 snapshot

Apps Script `ranked_config`가 운영 설정의 source입니다. 서버는 마지막으로 검증에 성공한 문서만 사용하며, 새 Match 생성 시 config generation과 전체 source payload, 후보 맵을 DB에 snapshot합니다. 진행 중인 Match는 이후 Spreadsheet 변경의 영향을 받지 않습니다.

맵 identity는 `canonicalLevelId`이고, 기존 Corum 데이터나 Ranked Pool의 `대체 맵 코드`는 `alternateLevelId`로 별도 반환합니다. 서버는 유효한 alternate를 `playableLevelId`로 반드시 우선하고 없을 때만 canonical로 fallback합니다. Pool/Qualifying/중복 판정과 history 연결은 canonical 기준이며 실제 다운로드·Round 진입·attempt 검증만 playable 기준입니다.

다음 값은 코드에 운영 기본값이 없습니다. 빈 값이면 config 검증이 실패하고 queue가 열리지 않습니다.

- CSMP tier별 최초 MMR Seed
- 표시 tier의 `minInclusive` / `maxExclusive`
- placement 횟수, placement/regular K-factor, expected-score divisor, 반올림 정책
- session, ready, reconnect, queue/match heartbeat, orphan attempt, result 화면 timeout
- 초기/초당 확장/최대 matchmaking MMR 범위
- ready/reconnect/restart 실패 정책

고정 규칙값(180초 Round, 10초 Final/LAST start window, 10초 private ban, BO3)은 명세값으로 초기 행을 만들지만 Match snapshot에 계속 포함됩니다.

## 2. Database

새 DB에 다음을 실행합니다.

```bash
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0001_initial_ranked.sql
```

기존 `v0.4.0-alpha.1` DB에는 Debug Match 분리 column/view를 추가하는 migration을 이어서 적용합니다.

```bash
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0002_debug_bot_match.sql
psql "$DATABASE_URL" -v ON_ERROR_STOP=1 -f ranked/migrations/0003_debug_rating_playable_maps.sql
```

Migration은 transaction 안에서 실행됩니다. 기존 운영 DB에 적용할 때는 일반적인 migration runner로 버전 관리하고, 같은 초기 migration을 재실행하지 마세요.

## 3. 서버 환경변수

실제 값은 배포 플랫폼의 secret manager에만 저장합니다. 저장소의 `.env.example`은 placeholder뿐입니다.

- `DATABASE_URL`: PostgreSQL 연결 문자열
- `RANKED_CONFIG_URL`: 배포된 Apps Script `ranked_config` URL
- `RANKED_CONFIG_REFRESH_MS`, `RANKED_CONFIG_FETCH_TIMEOUT_MS`: config refresh 제어값
- `RANKED_SESSION_TOKEN_SECRET`: production에서 최소 32자, 임의 생성값은 secret manager에서 생성
- `CORS_ORIGINS`: 필요한 origin만 쉼표로 지정
- `DISCORD_WEBHOOK_URL`: 비어 있으면 relay 비활성화
- `DISCORD_*`: Webhook을 켰을 때만 모두 명시하는 poll/batch/request/lease/retry/max-attempt 정책
- `ENABLE_DEBUG_BOT_MATCH`: 기본 false. non-production debug 서버에서만 true
- `DEBUG_BOT_PASSWORD`: debug route가 켜졌을 때만 주입하며 Production에는 배포하지 않음

서버는 production에서 HTTP Apps Script URL, 짧은 session secret, 공식 Discord 외 webhook 목적지를 거부합니다. Discord URL 자체는 로그나 message body에 넣지 않습니다.

## 4. Health와 관찰 항목

- `GET /health/live`: 프로세스 생존
- `GET /health/ready`: DB와 마지막 검증 config 상태
- config refresh 실패: 마지막 정상 snapshot이 있으면 기존 설정으로 계속 서비스하고 오류를 기록
- outbox: `delivered_at IS NULL AND abandoned_at IS NULL`은 재시도 예정, `abandoned_at IS NOT NULL`은 최대 시도 후 포기한 알림
- Match `result_applied_at`: MMR 반영 idempotency 기준
- `ranked_rounds.canonical_level_id` / `alternate_level_id` / `playable_level_id`: 경기 생성 시 고정된 맵 ID snapshot
- `ranked_attempts.played_level_id`: 실제 attempt가 snapshot playable ID에서 발생했는지 감사 가능

Discord 실패는 outbox worker에서만 처리되며 Match 판정 transaction을 되돌리지 않습니다.

### 관전용 runtime state

`currentAttemptProgress`는 `MatchRuntimeStatePort` 뒤의 메모리 구현에만 저장하며 DB row를 만들지 않습니다. 단일 서버 MVP에서는 이 값이 재시작 시 사라져도 점수와 판정에 영향이 없고, 살아 있는 client의 다음 telemetry로 복구됩니다. 수평 확장 시에는 같은 interface를 Redis 같은 TTL 기반 공유 저장소 구현으로 교체한 뒤 sticky routing 또는 공유 state 동작을 staging에서 검증하세요. Round가 끝나면 양쪽 임시 progress를 즉시 제거합니다.

## 5. 장애 정책

Ready timeout, reconnect timeout, restart recovery의 행동은 `Ranked Config`에서 반드시 선택합니다. 서버 시각과 `deadline_at`만 권위가 있으며 client 시각은 UI 보정용입니다. 재접속 grace가 지난 뒤 도착한 poll은 이미 확정된 forfeit/cancel 결과를 되돌리지 않습니다.

서버 재시작 전에 운영 정책과 DB backup을 확인합니다. Application bootstrap에서 진행 중 Match의 개별 config snapshot을 읽습니다. `restartRecoveryAction=CANCEL_MATCH`인 Match는 `SERVER_RESTART_RECOVERY` 사유로 취소하고, `RESUME`인 Match는 DB state/deadline을 유지해 다음 인증 poll에서 서버 시각 기준으로 상태를 전진시킵니다. `RESUME`는 staging에서 배포 중단시간과 reconnect 정책을 함께 검증하세요.

## 6. Production 전 체크리스트

- 모든 운영 미확정값 입력 및 config validation 성공
- 각 tier 후보 pool의 canonical-distinct 맵 수 충족
- Ranked와 CBF가 allowlist의 required+enabled 행인지 확인
- CBF `soft-toggle=false`, `click-on-steps=false`, `physics-bypass=false`
- staging PostgreSQL migration, two-client harness, 실제 Geode package 빌드 성공
- 일반 플레이에서 상대 progress 비공개, 2:1 LAST ATTEMPT trigger 화면에서만 관전 overlay 노출 확인
- deadline 전환이 없는 progress 요청이 PostgreSQL row/score/stateVersion을 만들지 않는지 확인
- HTTPS/TLS, reverse proxy request size/rate limit, DB backup/restore 확인
- Discord staging webhook으로 모든 7개 event type 확인
- Debug Bot을 사용한다면 전용 staging 서버/채널에서만 활성화하고, Release/Production에서는 flag와 route가 꺼졌는지 확인
- Debug Bot 결과도 실제 MMR/placement/통계/history에 반영되므로 개발 서버와 테스트 계정만 사용
- 실제 production secret이나 webhook URL이 Git history/빌드 로그에 없는지 확인
