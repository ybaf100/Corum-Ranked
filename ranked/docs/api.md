# Corum Ranked REST API

모든 응답 시각은 ISO-8601 UTC입니다. Poll/state 응답의 `serverNow`, `stateVersion`, `deadlineAt`을 사용하며 client wall clock으로 판정하지 않습니다.

## Public

| Method | Path | 설명 |
|---|---|---|
| `GET` | `/health` | 프로세스 liveness |
| `GET` | `/ready` | DB/config readiness |
| `GET` | `/api/ranked/config` | client용 rules, CBF, allowlist, queue 상태 |

## Session

`POST /api/ranked/session`

```json
{
  "gdAccountId": "12345",
  "gdUsername": "Player",
  "clientVersion": "v0.4.0-alpha.7",
  "installedMods": [
    {
      "id": "hwanhee1.corum_ranked",
      "version": "v0.4.0-alpha.7",
      "enabled": true,
      "loaded": true,
      "internal": false,
      "system": false
    }
  ]
}
```

성공 시 runtime-only `sessionToken`, 만료시각, 표시 tier/placement 상태를 반환합니다. 이후 session API는 `Authorization: Bearer <sessionToken>`이 필요합니다.

## Queue

| Method | Path | Body |
|---|---|---|
| `POST` | `/api/ranked/queue/join` | 최신 `installedMods` |
| `POST` | `/api/ranked/queue/leave` | 없음 |
| `POST` | `/api/ranked/queue/heartbeat` | 없음 |
| `GET` | `/api/ranked/queue/status` | 없음 |

`MATCHED` 응답은 `matchId`, player side, runtime-only `matchToken`을 반환합니다. Match API에는 Bearer token과 `x-match-token`을 함께 전송합니다.

## Match

| Method | Path | Body |
|---|---|---|
| `GET` | `/api/ranked/matches/:id/state` | 없음 |
| `POST` | `/api/ranked/matches/:id/ready` | 최신 `installedMods` |
| `POST` | `/api/ranked/matches/:id/ban` | `{ "canonicalLevelId": "..." }` 또는 `{}` |
| `POST` | `/api/ranked/matches/:id/attempt/start` | `playable levelId`, `clientEventId` |
| `POST` | `/api/ranked/matches/:id/attempt/progress` | `playable levelId`, `attemptId`, `progressPercent` 0..100 정수 |
| `POST` | `/api/ranked/matches/:id/attempt/end` | `playable levelId`, `attemptId`, 새 `clientEventId`, `progressPercent`, `cleared` |
| `POST` | `/api/ranked/matches/:id/heartbeat` | 없음 |

Start/End의 `clientEventId`와 DB unique constraint로 재시도를 idempotent하게 처리합니다. 서버는 accepted start 시각을 기록하고 attempt ID를 발급합니다. Client가 winner, 점수, MMR, deadline을 제출하는 API는 없습니다.

Round/Deathmatch map에는 `canonicalLevelId`, `alternateLevelId`, `playableLevelId`가 함께
있습니다. 유효한 alternate가 있으면 playable은 반드시 alternate이고, 없을 때만
canonical로 fallback합니다. 클라이언트는 playable을 실제로 열고 모든 attempt 요청의
`levelId`에 같은 값을 보내야 합니다. snapshot과 다르면 서버가 거부합니다.

`attempt/progress`는 정수 %가 변할 때만 보내고 client/server 양쪽에서 최대 10Hz로 제한되는 관전용 임시 telemetry입니다. 활성 attempt ID만 허용하며 PostgreSQL 기록, 점수, Clear 판정, 승패 판정에는 사용하지 않습니다. 서버가 재시작되면 이 임시 값은 사라질 수 있고 다음 client update로 다시 채워집니다. 관전 overlay가 활성화된 viewer만 match state poll을 250ms 간격으로 일시 가속하고, 나머지 상태는 운영자가 정한 일반 poll 간격을 유지합니다.

State 응답의 `spectator`는 viewer별로 필터링됩니다. 서버가 `내 Clears=2`, `상대 Clears=1`, `LAST_ATTEMPT_WINDOW`를 모두 확인한 trigger player에게만 `active=true`, `opponentName`, `currentProgress`를 반환합니다. 대상 player와 일반 경기 중에는 `{ "active": false }`만 반환하여 상대 현재 진행률을 공개하지 않습니다. `currentRound.map.qualifyingPercent`, 양쪽 `scores`와 `clears`, `deadlineAt`, `serverNow`가 플레이 HUD의 권위 데이터입니다.

BAN_PHASE 동안 두 player의 선택은 공개 state에 포함되지 않습니다. 확정 후에는 ban 결과만 공개하며, 선택된 3개 맵 중 현재 Round 맵만 노출합니다.

## Debug Bot Match (alpha only)

`POST /api/ranked/debug/bot-match`는 `ENABLE_DEBUG_BOT_MATCH=true`일 때만 생성할 수
있으며 일반 session bearer token이 필요합니다. 비밀번호는 query string이 아니라 JSON
body에만 보냅니다.

```json
{
  "password": "2008",
  "difficulty": "NORMAL",
  "scenario": "TRIGGER_LAST_ATTEMPT",
  "botBan": "RANDOM",
  "sendDiscordEvents": false,
  "installedMods": []
}
```

실제 요청의 `installedMods`에는 일반 Ready와 같은 전체 snapshot이 필요합니다. 응답의
`matchId`, `matchToken`, `side`를 일반 Match API에 그대로 사용합니다. state에는
`matchType: "DEBUG_BOT"`, `debug: true`가 표시됩니다. Bot Match도 일반 rating transaction을
사용하므로 MMR, visible Ranked Score, Placement, W/L, tier, history에 반영됩니다.
