# Survival M1 UI 보완 — beta.60

기준 main은 `4f2735534a0a2ce2c274b209d7128b749d2a15e4`다.
M1 beta.59 및 기존 검증 워크플로 수정이 main에 반영된 이후,
`fix/survival-m1-lobby-layout` 브랜치에서 UI 보완만 작업했다.
`feat/survival-m3-engine`의 진행 중인 변경은 별도 워크트리에 보존했다.

## 로비

- 플레이어 카드는 기존 Solo의 188×210, 매칭 패널은 188×196 비율을 재사용한다.
  모드 이름은 카드 위, 입장 버튼은 카드 아래에 놓는다.
  화면 비율에 맞춰 두 카드와 외부 버튼을 함께 축소하며 상단 메뉴·하단 메시지 공간을 예약한다.
- 도전과제와 기록은 모드 선택 전후 모두 상단 원래 위치에 표시한다.
  기록 버튼은 Solo 채널에서만 금색 배경이며, 나머지는 기존 일반 배경이다.
  하단 메시지 박스, Settings 및 개발 빌드의 Solo Debug 위치를 복원한다.
- Survival 진입은 해당 카드를 고정하고 뒤에서 매칭 패널이 오른쪽으로 나온다.
  Solo 진입은 Solo 카드가 오른쪽에서 왼쪽으로, Survival 카드가 왼쪽에서 오른쪽으로 움직인다.
  교차 구간 42–58%에서 Survival 카드와 같은 경로에 있는 매칭 패널을 교차 페이드한다.
  0.48초 Sine easing을 적용하며 Back은 동일 프레임 경로를 역순으로 사용한다.
- 하나의 `ModeLobbySlide` Cocos 액션이 카드·패널 위치와 투명도를 함께 관리한다.
  전환 중 카드, 매칭 메뉴, 상단 도구, Settings와 Back 입력을 잠근다.
  UI 재생성과 별도로 액션이 끝날 때까지 presentation 변경을 보류하며 배지/후광 노드를 유지한다.
- 티어 배지의 0크기 cascading 컨테이너에 중립 RGBA를 사용한다.
  기존 투명 컨테이너의 alpha 0이 부모 카드 페이드 후 배지 본체까지 없애던 문제를 수정한다.
  배지 본체와 후광의 자체 투명도·애니메이션을 보존한다.
- 비활성 모드의 입장 버튼과 매칭 버튼은 비활성 상태를 유지한다.
  OFF 모드의 조회 버튼, 기록·업적·리더보드 조회는 유지한다.
  Survival 매칭은 여전히 M1 미구현 상태이며 실제 경기나 가짜 매칭을 생성하지 않는다.

## 도전과제와 리더보드

- 모드 선택 전 도전과제를 열면 SHARED/공통을, Survival에서는 Survival을,
  Solo에서는 Solo Ranked를 초기 섹션으로 연다. 섹션 전환은 그대로 가능하다.
- 서버 정의와 클라이언트 한국어 탭/칭호 접두사를 공통으로 변경한다.
  `SHARED`, 업적 ID, 단계, Claim, 해금 내역, AP, 칭호 장착 API는 변경하지 않는다.
- 리더보드 선택 모드의 탭 배경을 노란색으로 표시한다.
  페이지 초기화·취소·응답 generation 검증은 유지한다.
- 승급전/GOLD 행 테두리의 정적 반경은 2.4/1.1/0.4에서 2.3/1.05/0.38로,
  이동 광선은 2.6/1.1/0.45에서 2.5/1.05/0.43으로 아주 조금 줄인다.
  밝기, 이동 속도, 광선 길이 및 일반 행/외곽 테두리는 유지한다.

## 통합 기록

`GET /api/ranked/history?scope=ALL|SOLO_RANKED|SURVIVAL`

- Ranked Bearer 세션이 필요하다. scope 생략 시 ALL이다.
  scope 이외의 쿼리 또는 임의 플레이어 ID를 받지 않는다.
  모드 활성 설정과 관계없이 현재 세션의 기록을 조회하며 `Cache-Control: no-store`를 사용한다.
- 기존 `MatchService.history`와 `SurvivalService.history`를 재사용한다.
  기존 `/api/ranked/matches`, `/api/ranked/survival/history` API는 유지한다.
  Solo의 기록 숨김 기준, 라운드/Death Match 상세 및 LP 표시 구조를 보존한다.
- 각 경기에는 `mode`를 제공하고 종료 시각 내림차순으로 최대 30건을 반환한다.
  같은 종료 시각은 match ID, mode 순으로 안정적으로 정렬한다.
  Survival M1의 실제 경기 기록은 빈 상태이며 LP ledger를 경기로 표시하지 않는다.
- 한 기록 화면에 Survival/Solo Ranked/전체 탭을 배치한다.
  모드 선택 전에는 전체, 채널에서는 해당 모드를 초기 탭으로 연다.
  전체/솔로 탭에서 기존 Placement 단계 기록도 표시한다.
  전체 탭의 경기에는 모드 이름을 붙이며 기존 상세 버튼의 스크롤/인덱스를 유지한다.
- 클라이언트 기록 요청은 공통 매칭 제어 요청과 분리한다.
  탭 변경 및 재시도 시 이전 요청을 취소하고 history generation과 session generation을 검증한다.
  동일 개수의 새 응답도 history revision으로 화면을 갱신한다.
  세션 종료 시 기록 요청도 취소한다.

## 운영 적용

- 버전은 모드, 서버 exact gate, workspace 및 lockfile 모두 `v0.5.0-beta.60`이다.
  서버와 클라이언트를 함께 배포한다. 자동 업데이트를 사용하는 경우 기존 운영 절차로
  Release/Debug immutable 패키지의 버전·SHA256·size 메타데이터를 갱신한다.
- 이번 보완의 신규 DB 마이그레이션은 없다. 기존 M1 `0021_survival_m1.sql`을 유지한다.
- Apps Script 코드와 설정은 변경하지 않았다. beta.59 M1 운영 적용을 완료했다면
  이번 UI 보완 때문에 Apps Script 재배포 또는 setup 재실행을 할 필요는 없다.
- 이미지/폰트 에셋 입력은 변경하지 않았다. 준비된 에셋의 불필요한 재다운로드를 추가하지 않는다.
- M3 경기 엔진, M4 봇/HUD, M5 매칭/온라인 정산은 이 UI 보완에 포함하지 않는다.

## 검증

- `npm run typecheck --prefix ranked`, `npm run build --prefix ranked` 통과.
- 규칙 테스트 136건, 서버 테스트 339건 검증. 전체 실행에서 변경 전 `[공용]`
  접두사를 기대하던 한 건을 `[공통]`으로 수정하고 Survival 통합 테스트 13건을 다시 통과했다.
  기존 Solo Queue, Placement, LP/MMR, 승급/강등, Death Match, AP/Claim/칭호,
  Debug Bot, 세션/환경/모드 활성화 및 DB 마이그레이션 테스트를 실행했다.
- 클라이언트 Node/native 테스트 165건 검증. 전체 실행 후 툴바 mock 필드와
  이전 카드 위치 63 기대값을 원래 UI의 68로 갱신하고 관련 22건을 다시 통과했다.
  새 테스트는 실제 액션의 교차/역방향 경로·입력 잠금, 배지 컨테이너의 페이드 복원,
  기록 탭 초기값·취소/오래된 응답 차단을 컴파일해 실행한다.
- CI와 같은 strict warning C++ 정책/domain 실행 파일 9개 통과.
- Python 패키징 테스트 15건, Ranked/Survival/기존 기록 시트 Apps Script 검증 통과.
- Geode 5.8.2 / GD 2.2081 / NDK r28c로 실제 Android64 Debug ON/ON과 OFF/OFF
  클라이언트를 각각 컴파일·링크했다. 외부 SDK/기존 코드 경고는 남아 있다.
- Windows, iOS, Android32 전체 빌드와 Geometry Dash 실기기 화면/애니메이션 확인은 수행하지 않았다.
  Cocos 경계 테스트는 기기 렌더링 검증을 대신하지 않는다.
- 사용자 요청에 따라 push 승인을 받기 전에는 원격 변경·PR 생성·새 GitHub Actions 실행을 하지 않는다.
  이 커밋의 GitHub Actions 결과는 아직 없다.

## 변경 파일

- `corum-ranked-mod/changelog.md`
- `corum-ranked-mod/mod.json`
- `corum-ranked-mod/src/RankedPopup.cpp`
- `corum-ranked-mod/src/RankedRuntime.cpp`
- `corum-ranked-mod/src/RankedRuntime.hpp`
- `corum-ranked-mod/src/domain/AchievementPresentation.hpp`
- `corum-ranked-mod/src/domain/RankedMode.hpp`
- `corum-ranked-mod/src/ui/ModeLobbyMotion.hpp`
- `corum-ranked-mod/src/ui/RankBadgeAssets.cpp`
- `corum-ranked-mod/src/ui/RankedLeaderboardPopup.cpp`
- `corum-ranked-mod/src/ui/RankedTranslations.hpp`
- `corum-ranked-mod/tests/AchievementSourceTest.mjs`
- `corum-ranked-mod/tests/HistoryNativeTest.mjs`
- `corum-ranked-mod/tests/LeaderboardNativeTest.mjs`
- `corum-ranked-mod/tests/ModeLobbyNativeTest.mjs`
- `corum-ranked-mod/tests/RankedInputNativeTest.mjs`
- `corum-ranked-mod/tests/RankedLobbyLifecycleNativeTest.mjs`
- `corum-ranked-mod/tests/ResultScoreNativeTest.mjs`
- `ranked/apps/server/package.json`
- `ranked/apps/server/src/app.module.ts`
- `ranked/apps/server/src/history/history.controller.ts`
- `ranked/apps/server/src/history/history.module.ts`
- `ranked/apps/server/src/history/history.service.ts`
- `ranked/apps/server/src/session/client-version.ts`
- `ranked/apps/server/test/client-version.test.ts`
- `ranked/apps/server/test/history.test.ts`
- `ranked/apps/server/test/survival-m1.integration.test.ts`
- `ranked/docs/survival-m1-ui-beta60.md`
- `ranked/docs/survival-m1.md`
- `ranked/package-lock.json`
- `ranked/package.json`
- `ranked/packages/rules/package.json`
- `ranked/packages/rules/src/achievements.ts`
