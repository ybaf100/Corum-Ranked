# Corum Survival M1 — beta.59

기준 remote main은 `2f8db8d7fe11f86940b699d496a680ad95e74053`이다.
작업 브랜치는 `feat/survival-m1-integration`, 클라이언트/서버 exact version은
`v0.5.0-beta.59`다. 기존 Corum-Ranked Geode 모드, NestJS 서버, PostgreSQL,
Apps Script 프로젝트 안에 통합한다.

## 구현 범위

- 타이틀의 기존 Ranked 버튼 → 버전 확인 → 캐시된 에셋 확인/다운로드 →
  허용/필수 모드 검사 → 통합 로비. 실패한 게이트 이후에는 세션을 생성하지 않는다.
  기존 진행 중인 Solo 세션/경기 복구가 우선한다.
- Survival 왼쪽/Solo 오른쪽 플레이어 카드. 공통 카드 렌더러가 기존 티어 후광,
  LP, 순위/Top %, 칭호, Solo 배치·승급전·강등 경고를 보존한다.
- 0.36초 Sine easing으로 카드 이동/페이드와 오른쪽 패널 펼침/접힘을 처리한다.
  전환 중 카드·메뉴·Back 입력을 잠그고 UI 교체를 미룬다. 패널의 평범한 CCNode를
  통과하는 실제 opacity 전파로 후광/버튼의 원래 alpha를 보존한다.
  대기열에서 Back을 누르면 취소 확인 뒤 역방향 전환한다. 취소와 경기 배정이
  경합하면 시작된 경기 복구가 우선한다.
- 모드별 독립 리더보드와 창 내부 탭. 전환 시 첫 페이지로 이동하고 요청 generation/
  모드 식별자를 검사해 오래된 응답을 버린다. 기존 페이지/Refresh/아이콘/후광은 유지한다.
- 업적의 명시적 서버 `scope`와 Solo Ranked / Survival / 공용 탭. 기존 20개 Solo
  업적과 Death Match 조건을 보존하고, Survival 탭은 실제 목록 없는 빈 화면이다.
  `gold_reached`만 공용으로 분류한다. 기존 해금·Claim·AP ledger의 유일성 키를 유지한다.
- AP는 계정 공용이며 장착 칭호는 하나다. 서버 칭호에 `[솔로 랭크]`, `[서바이벌]`,
  `[공용]` 접두사를 제공한다. 성장형은 기존 Roman 단계, 단일형은 단계 없이 표시한다.
- 독립 Survival Config 캐시/검증, 프로필/LP ledger, 순위 및 정산 확장 지점.
  M1의 Survival 경기/봇/온라인 큐 진입은 실제로 거절하며 성공을 가장하지 않는다.

## 운영 적용

1. 기존 PostgreSQL에 미적용 migration을 순서대로 적용한다. 이번 신규 migration은
   `ranked/migrations/0021_survival_m1.sql`이다. `sur_profiles`, `sur_lp_ledger` 및
   순위/이력 인덱스를 만들고 기존 업적 event source 제약에 `SHARED_TIER_REACHED`를
   추가한다. Solo LP/MMR/업적/AP 데이터를 초기화하거나 복사하지 않는다.
2. 기존 바인딩 Apps Script 프로젝트의 `Code.gs`를 갱신하고 `SurvivalConfig.gs`를
   추가한다. `RankedConfig.gs` 및 다른 기존 파일도 프로젝트에 유지한다.
3. `setupCorumSurvivalConfig()`를 실행한다. 탭/누락 행만 추가하고 기존 값·사용자 행·
   다른 탭을 보존한다. 반환된 `validation.ok`를 확인한다.
4. **기존 웹 앱 배포를 새 버전으로 수정**한다. 기존 배포 ID/URL을 유지하고
   `?action=survival_config` 응답이 `ok: true`인지 확인한다. GitHub push는 Apps Script
   재배포를 수행하지 않는다. `setupCorumRankedConfig()` 재실행은 이번 추가 때문에
   필수이지 않으며 Solo 설정을 다시 입력할 필요도 없다.
5. 서버와 Geode beta.59 패키지를 함께 적용한다. 자동 업데이트를 사용하는 운영은
   기존 Release/Debug별 immutable 패키지·SHA256·size 메타데이터를 beta.59로 게시한다.
   기존 exact-version gate를 유지하므로 이전 클라이언트는 업데이트 후 진입한다.
6. 기존 `RANKED_CONFIG_URL`, `RANKED_CONFIG_REFRESH_MS`,
   `RANKED_CONFIG_FETCH_TIMEOUT_MS`를 그대로 사용한다. 새 서버 환경 변수/비밀 키는
   필요 없다. 두 설정은 같은 배포를 각각 `ranked_config`/`survival_config`로 읽는다.
   정상 갱신한 설정이 주기적으로 반영되며, 검증 실패 시 각 모드의 마지막 정상
   스냅샷을 유지한다. `/api/ranked/config`의 `modes`에서 상태를 확인한다.

스프레드시트·운영 DB·서버 배포·공개 패키지 게시를 이 코드 작업에서 실행하지 않았다.
적용 전까지 운영 중인 서비스 설정은 그대로다.

## 독립 활성화

| Ranked Config.enabled | Survival Config.enabled | 신규 진입 |
| --- | --- | --- |
| TRUE | TRUE | 두 모드 로비 입장 가능; Survival 실제 매칭은 M1 미제공 |
| FALSE | TRUE | Solo 신규 큐/Debug Bot 금지, Survival 로비 가능 |
| TRUE | FALSE | Solo 정상, Survival OFF 조회 가능 |
| FALSE | FALSE | 공통 인증/로비 및 두 모드 조회 가능, 신규 매칭 금지 |

OFF 카드는 유지하고 큰 입장 버튼을 회색/비활성화한다. **OFF일 때만** 별도 `조회`
버튼을 보여 리더보드·기록·업적 메뉴를 펼친다. Survival 최초 OFF 조회도 실제
150 LP 프로필을 생성한다. 반복 조회/입장은 기존 프로필을 초기화하지 않는다.

Solo OFF 반영은 큐/Debug admission과 같은 fence에서 QUEUED 행 정리를 먼저 커밋하고
스냅샷을 공개한다. MATCHED 및 시작된 경기는 보존하고 시작 당시 스냅샷으로 끝까지
정산한다. Survival은 M1에서 실제 큐가 없어 정리할 참가자가 없으며 ON에서도
501 `SURVIVAL_MATCH_NOT_IMPLEMENTED`, OFF에서는 503 `SURVIVAL_DISABLED`다.

## Survival 설정과 LP

`Survival Config`의 Key / Value 행으로 관리한다.

| Key | 기본값 |
| --- | --- |
| enabled | FALSE |
| initialLp | 150 |
| placementEnabled | FALSE |
| promotionSeriesEnabled | FALSE |
| demotionProtectionEnabled | TRUE |
| matchPlayerCount | 16 |
| minimumPlayers | 8 |
| matchmakingMaxWaitSeconds | 180 |
| tierMatchingEnabled | FALSE |
| roundSeconds | 180 |
| round1Qualifiers | 10 |
| round2Qualifiers | 6 |
| round3Qualifiers | 3 |
| finalWinners | 1 |
| allowCutoffTies | TRUE |
| lastAttemptEnabled | FALSE |
| scoreMultiplier | 1 |
| tiebreakAttempts | 3 |
| tiebreakDecimals | 3 |
| minimumLevelId | 40000000 |
| demotionWarningRecoveryLp | 50 |
| demotionTargetOffsetLp | 50 |

`tierBands`는 별도 JSON 배열이다. 안정된 `tier` ID, 아이콘/공용 GOLD 판정을 위한
`group`, `minimumLp`를 명시하므로 향후 세분화된 티어를 추가할 수 있다.

```json
[
  {"tier":"RED","group":"RED","minimumLp":0},
  {"tier":"AQUA","group":"AQUA","minimumLp":300},
  {"tier":"BRONZE","group":"BRONZE","minimumLp":600},
  {"tier":"SILVER","group":"SILVER","minimumLp":900},
  {"tier":"GOLD","group":"GOLD","minimumLp":1200}
]
```

`placementLp`는 순위 1~16에 대응하는 JSON 배열이며 `matchPlayerCount`와 길이가 같아야 한다.

```json
[50,40,30,25,20,15,10,8,5,2,-10,-20,-30,-40,-50,-55]
```

공동 2위 2명은 둘 다 +30, 공동 11위 2명은 둘 다 -20이다. 점유 순위의 최저 LP를
순수 함수 `survivalPlacementLp`로 계산한다. 8~15명 실제 경기의 진출/LP 재계산은
M3/M5에서 별도 규칙으로 구현하며 M1에서는 확정하지 않는다.

승급은 다음 경계 이상이면 즉시 진행하고 초과 LP를 보존한다. 최초 하한 이탈은
하한으로 1회 보호하며 경고를 켠다. 경고 중 재이탈하면 **강등 전 하한 −50**으로
이동한다(SILVER 900 → BRONZE 850). 하한 +50 회복 시 경고를 해제하고 RED는 0 아래로
내려가지 않는다. 경고 카드에 회복 조건을 함께 표시한다.

동일 LP는 `RANK()` 공동 등수(1,2,2,4), 같은 LP의 정렬은 `lp_reached_at` 오름차순이다.
실제 LP 변경 시에만 도달 시각을 갱신한다. 보호로 LP가 그대로이거나 0 LP 변동이면
기존 시각을 유지한다. 다른 LP를 거쳐 돌아오면 새 도달 시각을 기록한다.

## 후속 연결점

- M3: 설정의 nominal 16→10→6→3→1 및 타이머/동점 모델을 사용해 실제 경기 엔진,
  랜덤 맵, 평균 LP 난이도, 커트라인과 추가 결승을 구현한다.
- M4: 별도 Survival 봇 15명과 점수 갱신/평균 티어 강제/인게임 HUD를 구현한다.
  기존 Solo Debug Bot은 유지하며 Survival Debug route는 아직 실제로 거절한다.
- M5: 8~16인 온라인 큐, 180초 대기 실행, 이탈/재접속 및 실제 경기 정산을 구현한다.
  `SurvivalService.settleLp`에 시작 시 캡처한 설정과 안정된 결과 event key를 전달한다.
  프로필·경고·ledger·공용 GOLD 해금을 한 트랜잭션으로 처리하고 동일 event key는
  결과를 재사용한다. 다른 LP를 재요청하면 거절한다. 공개 정산 API는 없다.
- `AchievementService.recordSharedTierReached`는 Survival GOLD용 공용 해금 확장점이다.
  기존 GOLD 해금/Claim/AP를 공유하고 Solo 승리·연승·Death Match 지표에는 반영하지 않는다.
- M5에서 Survival 큐를 도입할 때 해당 admission/설정 공개도 fence로 묶고 OFF 스냅샷을
  공개하기 전에 해당 모드의 QUEUED 참가자만 정리해야 한다. 시작된 경기는 유지한다.
- Survival 전용 업적 실제 목록/달성 로직 및 관전 기능은 추가하지 않았다.

## 검증 결과와 범위

- 변경 전 기준: rules 132 / server 318 통과. 기존 Apps Script 설정/기록 호환 검사 통과.
- 변경 후 전체: rules **136**, server **331** 통과. typecheck 및 workspace build 통과.
  마지막 LP 도달 시각/재진입 정렬 보강 후 Survival 통합 **13**개 재검사 통과.
- Solo 1:1/Placement/MMR/LP/승급·강등/Death Match/업적/AP/칭호/세션·복구/Debug Bot을
  기존 전체 서버 테스트로 회귀 검사했다. 실제 Solo 경기 도중 OFF 후 신규 매칭 거절,
  기존 두 라운드 완료·LP/MMR 정산을 추가로 확인했다. 1:1/Death Match harness 2개 통과.
- 클라이언트 전체 Node native/source 검사 **162**개 통과. 최종 문구/Back 처리 수정 후
  해당 업적/로비 검사도 재검증했다. 실제 모드 상태, Cocos action 콜백, 중간 이동 좌표,
  역방향 경로, 연타 입력 잠금, UI 교체 지연, 평범한 CCNode를 넘는 alpha 전파,
  다양한 화면 비율, 리더보드 빠른 전환/오래된 응답 폐기를 검사했다.
- 기존 엄격한 C++23 domain 실행 파일 9개, 패키지/릴리스 Python 검사 15개 통과.
  Apps Script Survival 4개와 기존 Ranked/기록 호환 검사 통과.
- 실제 Android64: Geode SDK 5.8.2, GD bindings 2.2081, Android NDK r28c/Clang 19로
  전체 소스 컴파일·링크를 수행했다. 외부 SDK 의존성의 누락된 표준 헤더 때문에
  로컬 빌드에 `-include type_traits`를 사용했으며 저장소/SDK 소스를 수정하지 않았다.
  Debug Bot ON / 기존 Debug Spectate OFF 및 두 Debug flag ON 구성이 통과했다.
  최종 로비 소스는 두 Debug flag OFF 구성에서도 컴파일·링크를 통과했다.
- 실제 CLI 생성 패키지에서 한글을 포함한 SD/HD/UHD 폰트 18개를 검증했다.
  폰트 RGBA 예산 7.72 / 25.85 / 95.91 MiB, 최대 atlas 4096 이내.
  기존 외부 visual pack `visuals-v1`을 그대로 사용하며 새 이미지 pack 게시가 필요 없다.
- PostgreSQL 통합 자동 검증은 저장소의 PGlite(PostgreSQL WASM) 환경이다.
  운영 PostgreSQL/스프레드시트는 수정하지 않았다.
- Windows/iOS 실기기 및 Android 실제 게임 화면/터치/후광·애니메이션은 이 환경에서
  실행하지 못했다. 타이틀→업데이트/에셋→환경 오류 흐름, 카드 선택/Back 연타,
  OFF 조회, 리더보드 전환, 업적 탭/칭호의 실기기 확인이 필요하다.
- 최종 GitHub Actions 결과와 PR/commit SHA는 PR 및 완료 보고에 기록한다.

## 트로피 리소스 확인

리더보드 버튼은 `rankIcon_1_001.png`를 사용한다. GD Community Hub의 원본 GD
`files/Original/Resources/GJ_GameSheet03-uhd.plist`와 대응 PNG를 함께 열어
금색 트로피 중앙의 검정 숫자 1을 시각적으로 확인했다.
spriteSize `{128,104}`, sourceSize `{132,106}`, textureRect
`{{1695,1997},{128,104}}`, 회전 없음. 원본 atlas는 재배포하지 않는다.

원본 확인 경로:
https://github.com/GD-Community-Hub/Assets/blob/main/files/Original/Resources/GJ_GameSheet03-uhd.plist

## 변경 파일

아래 목록은 이번 브랜치와 기준 main의 차이이며 생성된 빌드/폰트/테스트 캐시는 포함하지 않는다.

- `.github/workflows/build-ranked-debug.yml`
- `.github/workflows/ranked-ci.yml`
- `apps-script/Code.gs`
- `apps-script/README.md`
- `apps-script/SurvivalConfig.gs`
- `apps-script/validate-survival-config.mjs`
- `corum-ranked-mod/changelog.md`
- `corum-ranked-mod/mod.json`
- `corum-ranked-mod/src/RankedPopup.cpp`
- `corum-ranked-mod/src/RankedRuntime.cpp`
- `corum-ranked-mod/src/RankedRuntime.hpp`
- `corum-ranked-mod/src/domain/AchievementPresentation.hpp`
- `corum-ranked-mod/src/domain/RankedMode.hpp`
- `corum-ranked-mod/src/ui/ModeLobbyMotion.hpp`
- `corum-ranked-mod/src/ui/RankedLeaderboardPopup.cpp`
- `corum-ranked-mod/src/ui/RankedLeaderboardPopup.hpp`
- `corum-ranked-mod/src/ui/RankedTranslations.hpp`
- `corum-ranked-mod/tests/AchievementSourceTest.mjs`
- `corum-ranked-mod/tests/LeaderboardNativeTest.mjs`
- `corum-ranked-mod/tests/ModeLobbyNativeTest.mjs`
- `corum-ranked-mod/tests/RankedInputNativeTest.mjs`
- `corum-ranked-mod/tests/RankedLobbyLifecycleNativeTest.mjs`
- `corum-ranked-mod/tests/ResultScoreNativeTest.mjs`
- `corum-ranked-mod/tests/RoundPreparationNativeTest.mjs`
- `ranked/README.md`
- `ranked/apps/server/package.json`
- `ranked/apps/server/src/achievement/achievement.service.ts`
- `ranked/apps/server/src/app.module.ts`
- `ranked/apps/server/src/config/mode-admission.ts`
- `ranked/apps/server/src/config/ranked-config.controller.ts`
- `ranked/apps/server/src/config/ranked-config.module.ts`
- `ranked/apps/server/src/config/ranked-config.service.ts`
- `ranked/apps/server/src/config/survival-config.service.ts`
- `ranked/apps/server/src/database/database.service.ts`
- `ranked/apps/server/src/queue/queue.service.ts`
- `ranked/apps/server/src/session/client-version.ts`
- `ranked/apps/server/src/session/session.service.ts`
- `ranked/apps/server/src/survival/survival.controller.ts`
- `ranked/apps/server/src/survival/survival.module.ts`
- `ranked/apps/server/src/survival/survival.service.ts`
- `ranked/apps/server/test/client-version.test.ts`
- `ranked/apps/server/test/debug-bot.integration.test.ts`
- `ranked/apps/server/test/gameplay-controls.integration.test.ts`
- `ranked/apps/server/test/match-flow.integration.test.ts`
- `ranked/apps/server/test/migration.test.ts`
- `ranked/apps/server/test/queue.service.test.ts`
- `ranked/apps/server/test/survival-m1.integration.test.ts`
- `ranked/docs/api.md`
- `ranked/docs/survival-m1.md`
- `ranked/migrations/0021_survival_m1.sql`
- `ranked/package-lock.json`
- `ranked/package.json`
- `ranked/packages/rules/package.json`
- `ranked/packages/rules/src/achievements.ts`
- `ranked/packages/rules/src/index.ts`
- `ranked/packages/rules/src/survival.ts`
- `ranked/packages/rules/test/survival.test.ts`
