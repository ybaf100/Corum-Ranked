# Debug Bot Match

`Debug Bot Match`는 실제 Queue에 참가하지 않는 개발 전용 서버 harness입니다. Match row는 `match_type = 'DEBUG_BOT'`으로 저장되고 Bot/시나리오 이벤트는 일반 `MatchService`의 Ready, Ban, Attempt Start, Progress, Attempt End 경로를 통과합니다. 별도 점수 계산기나 별도 관전 HUD는 사용하지 않습니다.

## 개발 빌드에서 활성화

서버 프로세스를 시작하기 전에 다음 환경변수를 설정합니다.

```dotenv
ENABLE_DEBUG_BOT_MATCH=true
DEBUG_BOT_PASSWORD=2008
```

`ENABLE_DEBUG_BOT_MATCH`가 false이거나 비어 있으면 debug controller와 service를 Nest module에 등록하지 않습니다. `NODE_ENV=production`에서는 true 설정 자체를 서버 시작 시 거부합니다. 비밀번호는 POST body로만 받고 로그와 DB에는 저장하지 않습니다.

Geode 모드는 CMake configure 시 다음 중 하나로 활성화합니다.

```text
-DCORUM_RANKED_DEBUG_BOT_MATCH=ON
```

또는 build process 환경변수 `CORUM_RANKED_DEBUG_BOT_MATCH=ON`을 사용합니다. 현재 alpha의 `build-ranked-mod.yml`은 Debug Bot을 실제 기기에서 검증하기 위해 이 값을 ON으로 지정합니다.

## 제공 시나리오

- `NORMAL_MATCH`: 난이도 profile에 따른 자동 attempt
- `FORCE_BOT_ONE_CLEAR`: Bot의 승인 Clear를 한 번 만든 뒤 실패 attempt 반복
- `FORCE_BOT_TWO_CLEARS`: 일반 2-Clear 판정으로 Bot Round Win 유도
- `TRIGGER_LAST_ATTEMPT`: Player 2 Clear/Bot 1 Clear를 일반 attempt event로 준비
- `TRIGGER_ROUND_DRAW`: LAST ATTEMPT에서 Bot Clear를 발생시켜 Draw 유도
- `TRIGGER_ROUND_THREE`: Round 1 Player Win, Round 2 Bot Win 후 TIEBREAKER 진입
- `TRIGGER_DEATHMATCH`: Round 3 Draw 후 실제 3-Attempt Deathmatch 진입

Scenario 준비를 위해 Player 측 상태가 필요한 경우에도 DB/domain state를 직접 덮어쓰지 않고 debug driver가 Player 측의 정상 Start/End event를 `MatchService`에 제출합니다.

## Rating과 테스트 계정

Debug Bot Match는 Queue에는 들어가지 않지만, 완료 결과는 일반 PvP와 같은 `calculateMmrUpdate(ELO_V1)` 경로를 사용합니다. Hidden MMR, 1:1 변환 Ranked Score, 표시 tier, placement, W/L, leaderboard source와 Ranked Match History가 모두 갱신됩니다. 최초 Ranked 경기가 Bot Match이면 session 생성 시 기존 CSMP seed-once 절차를 거친 뒤 placement 1경기로 반영됩니다. 따라서 반드시 개발 서버와 테스트 계정을 사용하세요.

Bot rating은 `src/debug-bot/debug-bot.config.ts`의 한 곳에서 난이도별 offset으로 관리합니다. 현재 Easy/Normal/Hard는 각각 Player MMR 대비 -200/0/+200이며, 설정 tier의 최저 경계 아래로만 내려가지 않게 clamp합니다. Match row의 `mmr_b_before`와 `debug_config`에 실제 계산 rating/offset을 snapshot하며 Bot용 영구 profile은 만들지 않습니다.

Bot/PvP는 동일한 map resolver를 사용합니다. `alternateLevelId`가 있으면 `playableLevelId`로 우선 snapshot하고, 없을 때만 `canonicalLevelId`로 fallback합니다. Bot의 모든 Start/Progress/End도 해당 playable ID를 제출하므로 실제 클라이언트와 같은 서버 검증을 통과합니다.

## 격리 경계

- Queue row를 생성하거나 변경하지 않습니다.
- Match row에는 `match_type = 'DEBUG_BOT'`와 debug 설정을 남겨 PvP와 구분합니다. 이 표시는 rating 반영을 건너뛰는 조건이 아닙니다.
- Bot 자체는 영구 Ranked profile/leaderboard row를 만들지 않고, 사람 계정 결과만 정상 반영합니다.
- `ranked_public_match_history`에는 완료된 Debug Bot Match도 포함됩니다.
- progress는 기존 in-memory `MatchRuntimeStatePort`에만 저장합니다.
- Discord event는 Match 설정에서 명시적으로 ON인 경우에만 outbox에 생성하며 기본값은 OFF입니다.
- 서버 restart 시 완료되지 않은 Debug Match는 `DEBUG_SERVER_RESTART`로 취소합니다.

## Release에서 제거 또는 비활성화

Release Geode 바이너리에서는 `CORUM_RANKED_DEBUG_BOT_MATCH`를 설정하지 않거나 OFF로 둡니다. CMake가 `src/debug/*.cpp`를 source 목록에서 제외하고, 일반 UI/Runtime/PlayLayer의 guarded 호출도 전처리 단계에서 제거합니다. `.github/workflows/build-ranked-mod.yml`의 debug 환경변수도 Release workflow에서는 제거해야 합니다.

서버에서는 `ENABLE_DEBUG_BOT_MATCH=false`로 두고 `DEBUG_BOT_PASSWORD`를 배포하지 않습니다. Production은 debug 활성화를 거부합니다.

소스 자체에서도 완전히 제거할 때는 다음을 수행합니다.

1. `corum-ranked-mod/src/debug/`와 `corum-ranked-mod/tests/DebugBotDomainTest.cpp` 삭제
2. CMake의 `CORUM_RANKED_DEBUG_BOT_MATCH` 블록과 C++의 동일 guard 블록 삭제
3. `ranked/apps/server/src/debug-bot/`와 AppModule의 `DebugBotModule` import 삭제
4. Debug 전용 테스트와 workflow debug compile step 삭제

기존 `DEBUG_BOT` rating/history를 정식 서비스 데이터로 유지할지는 출시 직전에 별도로 결정해야 합니다. 운영 DB에서 행이나 rating을 삭제·되돌리려면 별도 보존 정책과 backup을 먼저 확정해야 하므로 release build 과정에서 자동 정리하지 않습니다.
