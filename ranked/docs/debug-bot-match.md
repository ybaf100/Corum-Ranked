# Debug Bot Match 개발 문서

`v0.4.0-alpha.6`의 Bot Match는 실제 Ranked 엔진을 시험하는 임시 개발 harness다.
비밀번호 입력값은 URL, 서버 로그, 일반 로그에 기록하지 않으며 HTTPS JSON body로만 보낸다.

## 활성화

클라이언트 빌드:

```text
-DCORUM_RANKED_DEBUG_BOT_MATCH=ON
```

서버 환경:

```text
ENABLE_DEBUG_BOT_MATCH=true
DEBUG_BOT_PASSWORD=2008
```

서버 route는 `POST /api/ranked/debug/bot-match`이고 일반 Queue에는 Bot을 넣지 않는다.
Easy/Normal/Hard MMR offset·Qualifying 도달 확률·Clear 확률·진행 속도는
`ranked/apps/server/.env.example`의 `DEBUG_BOT_*` 값으로 조정한다.

지원 scenario:

- `NORMAL_MATCH`
- `FORCE_BOT_1_CLEAR`
- `FORCE_BOT_2_CLEARS`
- `TRIGGER_LAST_ATTEMPT`
- `TRIGGER_ROUND_DRAW`
- `TRIGGER_ROUND_3`
- `TRIGGER_DEATHMATCH`

Bot이 만든 start/progress/end/clear는 일반 `MatchService`와 rules engine을 그대로 통과한다.
progress만 in-memory runtime state에 저장하며 PostgreSQL에 1% 단위 row를 쓰지 않는다.
경기는 DB에서 `match_type=DEBUG_BOT`로 식별하지만 플레이어 MMR, visible Ranked Score,
Placement, W/L, tier, history는 일반 PvP와 같은 transaction에서 갱신한다.

## 정식 Release에서 제거/비활성화

필수 두 단계:

1. 서버에서 `ENABLE_DEBUG_BOT_MATCH=false`로 설정하고 `DEBUG_BOT_PASSWORD`를 제거한다.
   route는 disabled 상태에서 404를 반환하며 Bot match를 생성하지 않는다.
2. Geode를 `-DCORUM_RANKED_DEBUG_BOT_MATCH=OFF`로 configure한다. 비밀번호 문자열,
   버튼, password/config popup, debug request 코드가 바이너리에서 전처리로 제외된다.

코드를 완전히 삭제하는 최종 정리에서는 다음을 제거한다.

- `apps/server/src/debug-bot/`와 `AppModule`의 `DebugBotModule`
- `QueueService.createDebugBotMatch` 및 debug 전용 type
- `corum-ranked-mod/src/DebugBotPopup.*`와 Runtime/Popup의 feature-guard block
- CMake option

DB의 `match_type`은 과거 개발 기록 식별에 유용하므로 출시 직전 데이터 유지 여부를
결정한 뒤 별도 migration으로 정리한다. 기존 Bot Match rating 기록은 flag를 끈다고
자동 롤백되지 않는다.
