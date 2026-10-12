# Corum Survival M4 — beta.62

기준 M3: `6a391bfd2df1b5650e62627180edbd7074e22ea8` (`feat/survival-m3-engine`).
기준 main: `71e4c8d73468e2cb68a7ef993d62b353da3d6281`.
작업 브랜치: `feat/survival-m4-debug-hud`. main 병합은 승인까지 대기한다.

## 실행 경로

기존 Debug 빌드의 Survival 채널 → Debug → Debug Bot Match에서 기존 비밀번호와
평균 그룹 RED–BRONZE / SILVER / GOLD를 선택한다. 서버가 실제 사용자 한 명과
Bot 1~15를 생성하고, M3의 실제 GD 검색·다운로드 제공자로 맵을 선정한다.
봇은 경기 내부 참가자이며 `ranked_players` 또는 공식 프로필을 만들지 않는다.

클라이언트는 기존 native 맵·음악·SFX 준비 경로를 사용한다. 서버 맵 ID, 버전,
별/Demon 메타데이터, decoded geometry 크기와 SHA-256을 다시 확인한다.
준비·native 진입·Attempt 시작 경계에서 허용/필수 모드를 재검증한다. 준비 완료 후
native PlayLayer에 진입하고 서버의 공통 3초 COUNTDOWN 동안 입력과 음악을 보류한다.
PLAYING 확인부터 실제 플레이 기록을 보내며, 플레이어와 봇은 동일 M3 reducer를 쓴다.

Survival OFF는 신규 디버그 경기만 차단한다. Solo OFF는 Survival 진입에 영향을
주지 않는다. 이미 시작한 경기는 캡처한 설정으로 끝까지 진행한다. 일반 릴리즈에는
Survival Debug/Force 메뉴와 디버그 API URL을 포함하지 않는다. 공개 Survival Queue는
여전히 미구현 응답을 반환하며 온라인 매칭으로 연결하지 않는다.

## 확정된 봇·경기 규칙

- 일반 라운드의 봇 초기 기록은 독립적인 정수 0~100%다. 40/90/120/150초마다
  봇별 서로 다른 ±5초 오프셋을 적용한다. 각 이벤트는 50% 확률로 현재 최고 기록부터
  100%까지 정수 재추첨, 50% 유지다. 감소하지 않으며 LP는 점수 확률에 영향을 주지 않는다.
- seed, round/map generation, 참가자 ID와 마지막 이벤트 인덱스를 PostgreSQL에
  저장한다. 중복 Tick, 동시 요청, 프로세스 재시작이 추첨을 다시 실행하지 않는다.
  늦은 worker Tick은 기한 내 예정된 봇 이벤트를 마감 판정 전에 처리한다. 실제 사용자의
  늦은 패킷은 M3의 서버 접수 마감 규칙을 그대로 따른다.
- 평균 그룹은 Survival Config의 명시적 tierBands group으로 계산한다. 실제 사용자 LP를
  포함한 평균을 맞추도록 15개 봇 LP를 분산 생성한다. 맞출 수 없는 경우 사용자 가상 LP를
  경기 스냅샷에만 적용하며 계정 LP는 변경하지 않는다.
- 일반 라운드는 180초, ×1, 원래 진행률의 최고 기록이다. LAST ATTEMPT는 없다.
  동점 커트라인 전원 진출과 목표 10/6/3/1, 정확히 16명 시작 조건은 M3 그대로다.
- 추가 Death Match 봇은 즉시 독립적인 3 Attempts를 시뮬레이션한다. 사용자는 실제
  최대 3회를 플레이하고 먼저 완료하면 실시간 기록을 보며 기다린다. 180초 또는 전원
  완료 시 판정한다. 접수된 최고 기록을 인정하고 미사용은 0%, 비교는 0.001% 반올림이다.
  사용자가 남은 공동 1위는 새 맵에서 반복한다.
- 사용자가 탈락하거나 명시적으로 항복하면 그 단계에서 디버그 경기를 즉시 종료한다.
  현재 단계 참가자는 그 단계 최고 기록, 이전 탈락자는 이전 단계 순서로 경쟁 순위를 받는다.
  봇 공동 1위는 그대로 종료한다. 봇만 남은 후속 라운드/추가 결승은 실행하지 않는다.
  항복은 고정 16위가 아니다. 맵 선정 전 전원 0%라면 공동 1위 16명이고 점유 마지막
  순위의 -55 LP가 적용된다.
- Force n은 비밀번호로 보호한 별도 디버그 동작이다. 생성 화면 또는 경기 중 Force n에서
  1~16을 지정하면 즉시 최종 결과로 이동한다. 서버가 순위와 LP를 계산하며 보상 delta나
  봇 참가자 ID를 클라이언트 정산 입력으로 받지 않는다.

## HUD와 결과

FPS 프레임·측정은 기존 코드를 유지한다. 그 아래에 1/2라운드 Top 3와 내 순위,
3라운드부터 현재 참가자 전원을 표시한다. 공동 등수도 서버 값을 사용한다.
참가자 ID별 노드를 유지하며 이름·점수 행이 함께 이동한다. 숫자 보간은 표시 전용이고
순위/진출 계산은 서버 점수를 사용한다. 새 기록 도착 시 진행 중 보간을 이어간다.

금/은/동/아쿠아/회색 순위 색상보다 탈락권 빨간색을 우선한다. 왼쪽 아래 큰 등수는
변경 시 확대·축소한다. 타이머 옆에는 최고 기록과 커트라인의 차이, 결승·추가 결승에서는
현재 1위와의 차이를 표시한다. 라운드가 바뀌면 이전 행/보간 상태를 정리한다.

최종 화면은 모든 순위와 순위별 LP를 표시한다. 실제 사용자만 M1 `settleLp`로 지급하며
자동 승급/강등 보호와 기존 RankTransitionLayer/ResultScoreLayer 모션을 재사용한다.
봇 LP 보상은 경기 결과에만 저장한다. 사용자 정산은 경기별 안정된 eventKey와 기존
LP ledger unique key로 한 번만 지급한다. 정산 후 결과 투영 중 중단돼도 재시도로 복구한다.
공통 GOLD만 공유하고 기존 Solo Death Match/승리 업적은 Survival 이벤트로 해금하지 않는다.

공통 세션을 다시 인증하면 미확인 경기/결과를 복구한다. 창을 닫거나 native 레벨에서
나가는 것만으로 항복 처리하지 않는다. 서버 타이머는 계속 흐른다. 종료되지 않은 Attempt는
서버 접수 기록으로 닫고 남은 횟수만 사용한다. native 체크포인트 복원과 온라인 이탈 처리는
M5 범위이며, 3 Attempts를 이미 사용했다면 추가 시도를 만들지 않는다.

## DB와 API

M1 `0021`, M3 `0022` 이후 `0023_survival_m4_debug.sql`을 적용한다.
새 `sur_debug_matches`는 인증 입장, RNG seed/cursors, 강제 순위, 결과, 정산/확인 시각을
저장한다. `sur_profiles.history_visible_since`는 모드별 기록 초기화를 지원하고
`sur_match_players.result_lp_delta`는 참가자의 결과 보상을 저장한다. 기존 Solo 테이블을
16인 모델로 바꾸지 않는다. Survival 카드와 공통 기록 창은 실제 완료 경기 요약을 조회한다.

인증 헤더는 기존 `Authorization: Bearer <ranked session token>`이다.

| 경로 (`/api/ranked/survival/debug`) | 동작 |
| --- | --- |
| POST `/matches` | password, requestId(UUID), averageGroup, installedMods, optional forceRank |
| GET `/active` | 내 미확인 경기/결과 복구 |
| GET `/matches/:id` | 서버 상태·맵·준비·전체 순위·최고 기록·타이머·정산 결과 |
| POST `/matches/:id/events` | eventId(UUID), type, roundKey, mapGeneration, levelId, Attempt/진행률/준비 증명 |
| POST `/matches/:id/force-rank` | password, rank 1~16; 즉시 서버 결과 확정 |
| POST `/matches/:id/acknowledge` | 정산된 결과 확인 후 복구 목록에서 제외 |

이벤트 type은 ASSETS_READY / PREPARATION_FAILED / ENTERED / ATTEMPT_START /
PROGRESS / ATTEMPT_END / FORFEIT이다. ASSETS_READY, ENTERED, ATTEMPT_START는
최신 installedMods를 함께 보낸다. FORFEIT만 맵 선정 전에도 허용한다.
모든 조작은 경기 소유자와 현재 살아 있는 세션을 확인한다. 조회는 no-store이며
다른 참가자의 점수/맵/최종 LP를 임의로 변경하는 엔드포인트는 없다.

## 모드별 Admin

기존 Admin 비밀번호/관리자 권한을 유지하며 Solo / Survival / Common 탭을 제공한다.
API action에 scope=`SOLO_RANKED` / `SURVIVAL` / `COMMON`을 보낸다.
파괴적 RESET_HISTORY/RESET_DB에는 scope가 필수다.

- Solo: LP/MMR/배치/경기 기록을 변경·초기화한다. Survival·공통 AP/칭호·계정·세션은 유지한다.
- Survival: Survival LP/티어/강등 경고/기록/경기를 변경·초기화한다. MMR 조작과 계정형
  봇 삭제는 제공하지 않는다. Solo·공통 데이터는 유지한다.
- Common: 공통 업적 해금/AP/칭호 초기화·전체 업적 해금 및 공통 오디오 설정을 제공한다.
  각 모드의 LP/경기 기록은 유지한다. 과거 경기를 자동 재백필해 즉시 재해금하지 않도록 표시한다.

선택 모드에 진행 중 경기 또는 미정산 Survival 결과가 있으면 해당 초기화를 거절한다.
다른 모드의 진행 중 경기 때문에 공통 인증을 끊지 않는다. Solo MMR이 비어 있더라도
세션 인증과 Survival은 작동하고, Solo 매칭의 배치/MMR 검사는 별도로 유지한다.

## 운영과 M5 연결

운영 DB/Apps Script/Render는 이 작업에서 변경하지 않는다. 적용할 때는 beta62 클라이언트와
서버 exact version gate를 함께 배포하고 0023을 먼저 적용한다. 기존 `ENABLE_DEBUG_BOT_MATCH=true`,
`DEBUG_BOT_PASSWORD`를 재사용하며 Survival Config의 enabled를 ON으로 설정해야 새 경기를
시작할 수 있다. 새 Apps Script 설정 행이나 재배포는 M4에서 필요하지 않다.

GD API 직접 검색은 M3 제공자를 사용한다. 실제 레벨/음악/SFX 연결을 제한하는 운영 환경이면
준비 완료를 위조하지 않고 재시도/재검색한다. 에셋 팩은 기존 캐시/버전을 재사용한다.

M5는 `SurvivalEngineService.createWithin`, `dispatch`, `viewForPlayer`와 공통 reducer에
온라인 참가자/준비/이탈 전송 어댑터를 연결한다. 정산 입력은 `survivalSettlementInputs`와
M1 `settleLp`를 재사용한다. M4의 seed/driver는 `sur_debug_matches`가 있는 경기에만 적용된다.
8~15인 진출·보상, 공개 큐, 180초 대기 실행, 온라인 재접속/항복 세션, 정식 멀티 배포 검증 및
Survival 관전은 구현하지 않는다. 기존 Solo 관전은 유지한다.

## 검증

최종 자동 검사와 실제 Android64 Debug ON/OFF 빌드·패키지 결과는 이 문서의 검증 기록에
추가한다. PGlite는 PostgreSQL 엔진을 사용하는 테스트 DB이며 운영 DB는 사용하지 않는다.
실제 Geometry Dash 기기에서 맵 플레이, 오디오, HUD의 화면 배치/모션을 확인한 것으로
보고하지 않는다. Windows/iOS 빌드와 실기기 연속 경기·앱 종료 복구 확인은 별도로 필요하다.

### 최종 검증 기록

- `npm run typecheck`, `npm run build`: 모든 workspace 통과.
- `npm test`: 규칙 **192/192**, 서버/DB/HTTP **386/386** 통과(48 server files).
  M4 integration 15개는 봇 생성/재현/시각/중복 Tick, 준비 재검색, 실제 탈락/항복,
  봇 공동 1위, Force 1~16, 평균 그룹/가상 LP, 실제 추가 결승, 정산/공통 GOLD/Claim,
  새 세션 복구 및 오래된 세션 차단을 포함한다. Solo 회귀도 같은 전체 검사에 포함한다.
- C++23 `-Wall -Wextra -Wpedantic -Werror`: 기존 9개 domain 실행 파일과 새 Survival HUD
  domain 실행 파일 통과. 새 검사는 행 선택/동점/색상/서수, 연속 점수 보간 및 SHA-256을 확인한다.
- 실제 production hook/UI를 실행하는 native doubles 검사 **169/169** 통과.
  최종 한국어 폰트 문구/기존 결과 UI 관련 24개도 다시 통과했다.
- Apps Script 기존 Ranked/record compatibility와 Survival additive setup 4개 통과.
  Python asset/release build·publish 회귀 **15/15** 통과.
- Geode 5.8.2 + Android NDK r28c: Android64 Debug Bot/Spectate ON 및 둘 다 OFF
  실제 컴파일·링크 통과. 두 native 바이너리를 각각 실제 `.geode`로 패키징했다.
  18개 SD/HD/UHD 폰트의 glyph/atlas 및 외부 이미지 분리 검사 통과.
  폰트 RGBA 예산은 품질별 **7.73 / 25.92 / 95.90 MiB**로 96 MiB 한도를 유지한다.
- 두 실제 package의 manifest beta62, CRC, native binary 일치 및 기존 native detour 목록을
  확인했다. OFF 바이너리에 Survival Debug URL/config popup이 없고 ON에만 포함된다.
- 실제 온라인 GD 제공자 검증은 M3 기록에 있으며 제공자 구현은 M4에서 변경하지 않았다.
  M4 통합 테스트는 결정적 맵 fixture와 실제 PostgreSQL/Nest HTTP 경로를 사용한다.
  기기에서 실제 GD 플레이/오디오/모션을 실행한 테스트와 혼동하지 않는다.

GitHub Actions는 push/PR 후 현재 상태만 확인하며 완료를 기다리거나 재실행하지 않는다.
워크플로 한도 때문에 원격 runner가 시작되지 않는 경우 로컬 검사 성공과 별도로 보고한다.

### 변경 파일

```text
.github/workflows/build-ranked-debug.yml
corum-ranked-mod/changelog.md
corum-ranked-mod/mod.json
corum-ranked-mod/src/PlayLayer.cpp
corum-ranked-mod/src/RankedLevelPreparation.cpp
corum-ranked-mod/src/RankedPopup.cpp
corum-ranked-mod/src/RankedRoundPreparation.cpp
corum-ranked-mod/src/RankedRuntime.cpp
corum-ranked-mod/src/RankedRuntime.hpp
corum-ranked-mod/src/SurvivalRuntime.cpp
corum-ranked-mod/src/SurvivalRuntime.hpp
corum-ranked-mod/src/debug/DebugAdminPopup.cpp
corum-ranked-mod/src/debug/DebugMenuPopup.cpp
corum-ranked-mod/src/debug/DebugMenuPopup.hpp
corum-ranked-mod/src/debug/SurvivalDebugPopup.cpp
corum-ranked-mod/src/domain/Sha256.hpp
corum-ranked-mod/src/domain/SurvivalHudPresentation.hpp
corum-ranked-mod/src/ui/RankAssetPackManager.cpp
corum-ranked-mod/src/ui/RankedConfirmPopup.hpp
corum-ranked-mod/src/ui/RankedTranslations.hpp
corum-ranked-mod/src/ui/SurvivalGameLayer.cpp
corum-ranked-mod/src/ui/SurvivalHud.cpp
corum-ranked-mod/src/ui/SurvivalHud.hpp
corum-ranked-mod/tests/DebugAdminNativeTest.mjs
corum-ranked-mod/tests/DebugSpectateRuntimeNativeTest.mjs
corum-ranked-mod/tests/EntryGameplayNativeTest.mjs
corum-ranked-mod/tests/LevelPreparationSourceTest.mjs
corum-ranked-mod/tests/LobbyToolbarNativeTest.mjs
corum-ranked-mod/tests/RankAssetPackNativeTest.mjs
corum-ranked-mod/tests/RankedInputNativeTest.mjs
corum-ranked-mod/tests/RankedLobbyLifecycleNativeTest.mjs
corum-ranked-mod/tests/RankedPauseNativeTest.mjs
corum-ranked-mod/tests/RoundPreparationNativeTest.mjs
corum-ranked-mod/tests/SpectatorAudioNativeTest.mjs
corum-ranked-mod/tests/SpectatorNativeHookTest.mjs
corum-ranked-mod/tests/SurvivalHudPresentationTest.cpp
ranked/README.md
ranked/apps/server/package.json
ranked/apps/server/src/admin/admin.dto.ts
ranked/apps/server/src/admin/admin.service.ts
ranked/apps/server/src/database/database.service.ts
ranked/apps/server/src/placement/placement.service.ts
ranked/apps/server/src/queue/queue.service.ts
ranked/apps/server/src/session/client-version.ts
ranked/apps/server/src/session/session.guard.ts
ranked/apps/server/src/survival/survival-debug.controller.ts
ranked/apps/server/src/survival/survival-debug.dto.ts
ranked/apps/server/src/survival/survival-debug.service.ts
ranked/apps/server/src/survival/survival-engine.service.ts
ranked/apps/server/src/survival/survival.module.ts
ranked/apps/server/src/survival/survival.service.ts
ranked/apps/server/test/admin.integration.test.ts
ranked/apps/server/test/client-version.test.ts
ranked/apps/server/test/deathmatch-flow.integration.test.ts
ranked/apps/server/test/debug-bot.integration.test.ts
ranked/apps/server/test/gameplay-controls.integration.test.ts
ranked/apps/server/test/match-flow.integration.test.ts
ranked/apps/server/test/placement-flow.integration.test.ts
ranked/apps/server/test/queue.service.test.ts
ranked/apps/server/test/session.service.test.ts
ranked/apps/server/test/survival-debug.integration.test.ts
ranked/docs/api.md
ranked/docs/survival-m4.md
ranked/migrations/0023_survival_m4_debug.sql
ranked/package-lock.json
ranked/package.json
ranked/packages/rules/package.json
ranked/packages/rules/src/index.ts
ranked/packages/rules/src/survival-debug.ts
ranked/packages/rules/src/survival-match.ts
ranked/packages/rules/test/survival-debug.test.ts
```
