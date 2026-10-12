# Corum Survival M3 — beta.61

기준 main: `71e4c8d73468e2cb68a7ef993d62b353da3d6281` (M1 beta.60 UI 포함).
작업 브랜치: `feat/survival-m3-engine`. main 병합은 사용자 승인까지 대기한다.
기존 Geode 모드, NestJS 서버, PostgreSQL, Survival Config를 그대로 사용한다.

## 실행 범위

실제 GD 검색·다운로드 제공자와 영속 경기 엔진을 구현했다. 서버가 맵, 준비 장벽,
카운트다운, 라운드 마감, 진출자, 반복 추가 결승, 단독 우승자 및 최종 공동 순위를
확정한다. 봇/사용자는 같은 판정 엔진을 사용하며, 테스트용 맵 제공자와 실제 제공자를
분리한다. 고정 맵 배열이나 임의 레벨 ID를 운영 경로에 사용하지 않는다.

공개 Survival Queue/Debug Bot 진입은 ON에서 501, OFF에서 503으로 유지한다.
M3의 내부 생성 포트를 공개 온라인 매칭으로 연결하지 않았다. M4 봇 생성/HUD 및
M5 큐/접속 관리/공식 LP·업적 정산이 준비되기 전에는 실제 플레이 진입을 열지 않는다.

## 확정 규칙

- M3 생성은 **정확히 16명**이다. 8~15명 및 17명은 거절한다. Config의 minimumPlayers=8,
  matchmakingMaxWaitSeconds=180은 유지하지만 축소 경기 진출·LP 규칙은 확정하지 않았다.
- 일반 라운드는 16→10→6→3→1, 각각 서버 기준 180초, 배율 ×1이다.
  Attempt 재시작·사망으로 최고 기록은 감소하지 않는다. 일반 진행률은 반올림하지 않는다.
- 컷 동점자 모두 통과하며 다음 목표는 여전히 6/3/1이다. 현재 인원이 목표 이하이면
  전원 통과한다. 이미 탈락한 플레이어를 다음 라운드 명단에 추가하지 않는다.
- 준비/진입/3초 공통 카운트다운은 180초에 포함하지 않는다. 진행률은 경기 행 잠금을
  얻은 뒤 샘플링한 서버 접수 시각이 `endsAt` 미만일 때만 반영한다. 정확히 마감 시각부터
  패킷은 늦은 것이다. 클라이언트 시각은 받지 않으며 오래된 기록으로 결과를 변경하지 않는다.
- 평균 LP는 최초 확정 Survival LP 합계/16이다. Solo LP/MMR을 사용하지 않는다.
  LP 스냅샷, 평균, 난이도 그룹, 설정 및 generation을 경기 기록에 저장하고 끝까지 유지한다.
  tierBands의 명시적 group을 사용하므로 세부 티어가 추가돼도 표시 이름으로 추측하지 않는다.
- 기본 평균 경계: 899 이하 RED–BRONZE, 900~1199 SILVER, 1200 이상 GOLD.

| 단계 | RED–BRONZE | SILVER | GOLD |
| --- | --- | --- | --- |
| Round 1 | 4~6★ | 5~7★ | 6~8★ |
| Round 2 | 5~7★ | 6~8★ | 7~9★ |
| Round 3 | 6~8★ | 7~9★ | 8~9★ + Easy Demon |
| Final | 7~9★ + Easy Demon | Easy~Medium Demon | Easy~Hard Demon |
| 추가 결승 | Medium Demon | Hard Demon | Insane Demon |

모든 범위는 양끝을 포함한다. 일반 별과 Demon 세부 난이도를 구분한다.
GD 2.2081의 `DemonDifficultyType`: Hard=0, Easy=3, Medium=4, Insane=5, Extreme=6.
검색 demonFilter의 1/2/3/4/5와 혼용하지 않는다. Geode 바인딩을 확인했고 클라이언트
어댑터의 static_assert가 다른 바인딩 코드값을 빌드에서 검출한다.

## 준비 지연과 귀책

1. 서버가 맵을 저장하면 PREPARING을 시작한다. 맵·음악·SFX 준비 중 10초 경고,
   15초 마감이다. 15초까지 준비한 사람이 있으면 미준비자만 항복 처리한다.
   아무도 준비하지 못했거나 전원이 실패를 보고하면 귀책 없이 새 맵을 선정한다.
2. 생존 참가자 전원의 준비 완료 후 서버가 native 맵 진입을 요청한다(ENTERING).
   **이 요청부터 5초** 동안 아무도 실제 진입하지 않으면 맵을 다시 선정한다.
3. **최초 실제 맵 진입 확인부터 10초** 동안 진입하지 못한 사람만 항복 처리한다.
   준비/진입을 끝낸 참가자에게 다른 사람의 지연 책임을 전가하지 않는다.
4. 생존 참가자 전원이 실제 맵에 진입했으면 3초 공통 카운트다운 후 시작한다.
   항복 판정 후 한 명만 남으면 즉시 우승, 동시에 전원 항복 판정이면 경기 취소다.

서버가 맵 자체의 오류를 확인했을 때만 MAP_INVALIDATED를 사용해 준비 중 재검색한다.
일반 클라이언트의 실패 보고 하나로 전체 맵을 바꾸지 않는다. PLAYING 이후 맵 변경은
거절한다. 온라인 세션 이탈/강제 종료 실행은 M5가 이 준비 모델에 연결한다.

## 결승, 순위, 정산 입력

Final 단독 1위면 종료한다. 공동 1위 전원만 Death Match에 들어가며 각자 최대
3 Attempts, 최고 기록, 180초 제한이다. 먼저 세 번 마친 사람은 남은 참가자의 실시간
순위를 보며 기다린다. 전원이 세 번 완료하면 조기 판정한다. 시간 마감에는 진행 중
시도의 서버 접수 최고 기록을 인정하고 미사용 시도는 0%로 판정한다.

Death Match만 0.001% 반올림 후 정수 밀리퍼센트로 비교한다(94.3705→94371).
Solo의 `bestPercentOfThree` 판정 로직을 재사용하되 Solo의 기존 입력/비교 의미는
변경하지 않았다. 동점이면 최고 동점자만 새 맵에서 같은 추가 결승 난이도로 반복한다.
100% 동점도 재경기하며 최종 우승자는 항상 한 명이다.

더 깊은 단계에 진출한 사람이 앞선다. 같은 탈락 단계는 그 단계 최고 기록으로
표준 경쟁 순위(1,2,2,4)를 받는다. 추가 결승에서 먼저 탈락한 사람보다 다음 추가
결승까지 남은 사람이 앞선다. `survivalSettlementInputs`는 participant/account,
rank, tiedPlayers, occupiedLastRank, LP delta, 안정된 eventKey, configGeneration을
산출한다. 공동 2위 두 명은 둘 다 3위 LP다. **M3는 LP/AP/업적을 지급하지 않는다.**

## 경기 엔진 및 API

상태: MAP_SELECTION → PREPARING → ENTERING → COUNTDOWN → PLAYING → ROUND_RESULT
→ 다음 MAP_SELECTION. 단독 우승 FINAL_RESULT, 전원 항복 CANCELLED.
추가 결승은 phase=TIEBREAK로 동일 준비/플레이 상태를 사용하며 sequence를 저장한다.

`SurvivalEngineService`는 기존 SurvivalModule의 내부 application port다.

```ts
const match = await engine.create("stable-admission-key", entrants); // exactly 16
// HUMAN: {participantId, kind:"HUMAN", playerId}; Survival LP is read under lock.
// BOT: {participantId, kind:"BOT", name, lp}; M4 supplies bots, M3 creates none.
await engine.selectMap(match.id); // real GD provider; automatic worker also runs
await engine.dispatch(match.id, "stable-event-key", command);
const persisted = await engine.get(match.id);
const hud = await engine.viewForPlayer(match.id, accountId);
```

명령은 MAP_SELECTED(서버 제공자), ASSETS_READY, PREPARATION_FAILED, ENTERED,
ATTEMPT_START, PROGRESS, ATTEMPT_END, MAP_INVALIDATED(검증된 서버 결정),
FORFEIT(신뢰된 세션 어댑터의 participantIds/귀책 사유), TICK이다.
참가자 명령에는 participantId, roundKey, mapGeneration, levelId를 포함한다.
ASSETS_READY의 certificate는 서버 확정 맵 메타데이터/버전/geometry SHA256/크기와
일치해야 한다. 실제 에셋 완료 및 native 진입 증거를 보내는 전송 어댑터는 M4/M5가 연결한다.

동일 creation key/event key는 한 번 적용한다. 같은 키의 다른 payload는 거절한다.
경기 행 FOR UPDATE 아래에서 reducer·JSON 스냅샷·조회용 테이블·명령 journal을 함께
커밋한다. 이미 결과가 난 라운드를 중복 종료하거나 다음 라운드를 중복 생성하지 않는다.
설정 OFF 공개와 신규 내부 생성은 같은 admission fence를 사용한다. 시작된 경기는
기존 스냅샷으로 계속 진행한다. Solo의 설정/대기열/정산을 호출하지 않는다.

| 공개 API | 권한 | 내용 |
| --- | --- | --- |
| GET `/api/ranked/survival/matches/:matchId` | 기존 Ranked Session + 해당 참가 계정 | no-store HUD/경기 상태 |

생성/점수/항복/맵 채택/정산의 공개 HTTP API는 없다. HUD 응답에는 serverNow,
준비 경고·마감/진입/시작/종료 시각, 남은 초, 현재 라운드 전체 standings, top3,
myRank, 최고 진행률, Attempts/대기 여부, cutoffScore/passing, 최종 1위 점수,
참가자 이름·유형·LP 스냅샷/탈락/최종 등수, winner 및 settlementInputs가 있다.

## 맵 제공자 및 복구

`GdSurvivalMapProvider`는 GD getGJLevels21/downloadGJLevel22 API를 직접 사용한다.
첫 검색 페이지로 실제 페이지 범위를 확인하고 무작위 미방문 페이지/후보를 검증한다.
모든 후보는 ID≥40,000,000, Rated, 비Auto, 비Platformer, 단계별 Predicate를 통과해야 한다.
다운로드 ID/별/Demon/버전이 카탈로그와 일치해야 하며 base64+gzip/zlib의 32MiB 이내
실제 geometry를 검사하고 SHA256/바이트 수를 저장한다.

EMPTY_SEARCH, API_ERROR, INVALID_METADATA, DOWNLOAD_FAILED, INVALID_DATA,
DUPLICATE_CANDIDATES, NO_ELIGIBLE_MAP을 구분해 저장한다. 이미 사용한 레벨은
같은 경기에서 제외한다. 후보/페이지 소진 또는 API 오류는 조건을 완화하지 않고
2초 이후 재검색한다. 요청당 8초, 한 선정 작업 30초 예산, DB lease 40초다.
만료 lease/이전 generation의 응답은 새 맵을 덮어쓰지 못한다.

250ms worker는 DB의 준비·시작·마감 시각을 복구하며 최대 네 개의 독립 맵 작업을
진행한다. 느린 GD 응답을 기다리는 동안 다른 경기의 카운트다운/마감을 막지 않는다.
재시작 후 스냅샷과 만료 lease를 읽어 진행한다. 맵 선정 실패 중에는 플레이 시간이 없다.

클라이언트 `survivalNativeMapMatches`는 실제 Saved GJGameLevel의 ID/버전/별/Demon,
Auto/Platformer 및 다운로드 완료·비어 있지 않은 native 데이터를 검증한다.
GD 객체를 합성하거나 서버 확정 맵을 다른 클라이언트 랜덤 맵으로 바꾸지 않는다.
M4에서 다운로드 완료와 native PlayLayer 진입 직전에 호출한다. 음악/SFX 및 HUD
렌더링은 기존 준비 기능을 사용해 M4에서 연결한다.

GMDEndless `fix/m2h-native-map-load`의 MapSelector/MapService/NativeRequests/LevelData
각 cpp/hpp를 읽기 전용으로 참고했다. 네이티브 메타데이터, 페이지 범위, 중복 제외,
실패 분류 및 geometry 검증을 Survival의 범용 Predicate와 서버 선정 권한에 맞췄다.
해당 저장소는 수정하지 않았다.

## DB 및 운영 적용

신규 migration: `ranked/migrations/0022_survival_m3_engine.sql`.
sur_matches, sur_match_players, sur_rounds, sur_round_players, sur_attempts,
sur_tiebreaks, sur_tiebreak_attempts, sur_match_commands를 생성한다. 외래 키, 행별
유일성, 활성 account/Attempt 인덱스, 마감·진행률 제약을 포함한다.
sur_profiles/sur_lp_ledger 및 Solo 테이블/기록은 유지한다. DB startup probe도 갱신했다.

운영 반영 시 미적용 migration을 순서대로 실행하고 서버/Geode beta.61을 함께 적용한다.
추가 환경 변수·운영 비밀 키는 필요 없다. 서버의 GD HTTPS POST/응답 다운로드 접속은
허용되어야 한다. 기존 Survival Config의 기본 규칙과 tierBands/placementLp를 유지한다.
M3 엔진은 명세와 다른 16명/180초/진출수/배율/Attempts 설정으로 생성을 요청하면 거절한다.
Survival enabled=TRUE로 바꾸어도 아직 공개 온라인 매칭을 열지 않는다.

M3는 Apps Script 파일/설정 행을 추가하지 않으므로 **M1 배포가 정상이라면 재배포나
setup 재실행이 필요 없다**. 외부 이미지 pack은 그대로이며 한글 폰트는 모드 패키지에서
다시 생성한다. 운영 DB, 스프레드시트, 서버 및 공개 Release를 이 작업에서 배포하지 않는다.

## 로비 보완 및 후속 연결

188×210 플레이어 카드와 같은 크기의 매칭 패널을 사용한다. Enter/Browse 대신 카드
전체를 클릭하며 OFF 채널 조회도 가능하다. OFF 카드 선택 시 자체 Corum UI/한글 폰트의
“이 모드는 운영 중지 상태입니다. 확인을 누르면 조회 화면으로 이동합니다.” 안내창을
먼저 표시한다. 확인하면 조회 채널로 이동하고 취소/Back이면 선택 화면을 유지한다.
팝업 연타·확인 중 Back·이전 팝업 콜백·세션 복구·화면 종료를 검증한다.
하단 파란 메시지창에서 모드 선택·OFF 안내를
제공하고 메시지를 한글화했다. Solo History/선택된 리더보드는 어두운 박스에 금색
테두리·글자를 사용한다. 기록 버튼 교체 후 updateSprite 및 배치를 갱신해 이동/겹침을
방지한다. 매칭 제목/아이콘/힌트를 가운데 정렬하고 GOLD/승급전 테두리만 소폭 줄였다.
기존 교차/역방향 애니메이션, 공통 도전과제/칭호, 통합 기록 탭은 유지한다.

M4: engine의 BOT port와 같은 Attempt 명령, native 맵 검증, 준비 장벽, HUD 응답을
사용해 실제 사용자+15봇/평균 난이도 강제/자동 점수/인게임 화면을 연결한다. M3에는
봇 생성기나 평균 LP 강제 옵션, Survival HUD 렌더링이 없다.

M5: HUMAN port에 확정 큐 명단을 넘겨 LP 스냅샷을 잠금 아래 읽는다. participant 명령
전송 권한/세션/이탈·재접속 및 FORFEIT 귀책을 검증한다. OFF 공개 전에 해당 QUEUED
참가자만 제거하도록 같은 admission fence를 확장한다. FINAL_RESULT의 안정된 eventKey와
config snapshot을 기존 `SurvivalService.settleLp`에 연결하고 공식 history/LP/AP를 반영한다.
8~15명 진출·LP 규칙은 추가 합의 후 구현한다. Survival 관전은 구현하지 않았다.

## 검증

로컬 검증 결과(Solo 및 M1 회귀 포함):

| 검사 | 결과 |
| --- | --- |
| 전체 rules | 187/187 통과 |
| 전체 server | 369/369 통과, 47 파일 |
| 전체 클라이언트 native/source | 169/169 통과 |
| workspace typecheck/build | 통과 |
| strict C++ domain/Debug/관전/준비 | 9 실행 파일 통과 |
| 패키징·릴리스·이미지 pack Python | 15/15 통과 |
| Survival Apps Script | 4/4 통과 |
| 기존 Ranked Apps Script/기록 호환 | 통과 |
| Android64 공식(Debug OFF) 및 Debug ON | 전체 컴파일·링크 통과 |
| 실제 공식 Android64 패키지 | native hook·외부 이미지 분리 검사 통과 |
| SD/HD/UHD 한글 폰트 18 bundles | 누락/크기 검사 통과, RGBA 7.72/25.85/95.91 MiB |

신규 Survival 규칙/맵 제공자/영속 서버 통합/클라이언트 맵 검증/로비 버튼/닫힌 채널
팝업 테스트는 모두 위 전체 실행에 포함한다. 빌드와 회귀의 로그는 실행 작업공간에서
보관했다. GitHub Actions는 push/PR 후 상태를 확인하되 완료까지 기다리지 않으며,
로컬 성공을 CI 성공으로 보고하지 않는다.

```sh
npm run typecheck --prefix ranked
npm test --prefix ranked
npm run build --prefix ranked
node --test --test-concurrency=2 corum-ranked-mod/tests/*.mjs
node --test apps-script/validate-survival-config.mjs
node apps-script/validate-ranked-config.mjs
node apps-script/validate-record-sheet-compat.mjs
```

규칙 테스트는 전체 라운드/동점 컷/마감/초기 인원/평균 경계/세부 티어/15개 난이도 범위,
2·3·16명 결승 동점/반복 100%/0.001% 차이/미사용 0%/준비 지연·귀책을 검사한다.
서버 통합은 전체 migration을 적용한 PGlite(PostgreSQL WASM), 실제 Nest HTTP/SessionGuard,
LP·Solo 불변, OFF 이후 완주, 원자적 중복 처리, 재시작, 늦은 맵 lease, 느린 GD 검색과
다른 경기 마감의 독립성을 검사한다. 맵 제공자 실패 fixture는 실제 제공자와 분리했다.

실제 GD 제공자 실행에서 RED–BRONZE R1 5★ ID `74148089`, GOLD TIEBREAK Insane Demon
ID `86605922`의 메타데이터·다운로드·geometry SHA256 검증을 통과했다. 실시간 GD 카탈로그는
변하므로 CI는 네트워크 fixture를 사용한다. 모든 범위의 live 조회를 했다고 주장하지 않는다.

Android64 Geode 5.8.2/GD 2.2081/NDK r28c Debug ON/OFF 빌드를 검증했다.
외부 SDK 표준 헤더 보완용 로컬 `-include type_traits` 외 SDK를 수정하지 않는다.
실제 Android/iOS/Windows GD 실행, 터치·화면·애니메이션 및 온라인 Survival 경기는
이 환경에서 검증할 수 없으며 완료로 보고하지 않는다.
