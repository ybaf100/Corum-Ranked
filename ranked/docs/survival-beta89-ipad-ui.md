# Survival beta.89 — iPad UI와 리더보드 제스처

기준 원격 main: `a957df123c53329f0c800b528ecac0173cfe3a08` (beta.88).
작업 브랜치: `fix/ipad-podium-leaderboard-settings-beta89`.
실기 증거: iOS GD 2.2081 / Geode 5.11.2 / mod beta.88의 `Geode 2026-10-20 18.22.29.log`와 사용자가 보낸 시상식·설정·Credits 스크린샷.

## 원인과 구현

- 시상식의 티어/LP stamp를 투명 `CCLayerColor`로 만들고 `setModeLobbyOpacity()`를 적용했다. 이 함수는 루트의 opacity 자체를 올리므로 원래 alpha 0인 검은 fill도 불투명해진다. `CCLayer`는 기본적으로 position에서 anchor를 무시하여, 크기의 절반만큼 오른쪽/위로 치우친다. 배경을 그리지 않는 `CCNode`에 명시적인 크기와 중앙 anchor를 부여하고 `propagateModeOpacity()`로 자식의 고유 alpha를 보존한다. 기존 단상 상승/아이콘 낙하/3→2→1 순서, 프로필, GOLD ambient, 정산 후 LP/증감량은 유지한다.
- 리더보드의 전체 카드 `CCMenu`가 첫 touch began을 삼킨다. 나중에 drag를 판별해 메뉴를 취소해도 ScrollLayer는 began을 받지 못해 스크롤할 수 없다. 카드 메뉴를 제거하고 `LeaderboardScrollLayer` 하나가 기존 네이티브 began/moved/ended/cancelled를 모두 처리한다. 같은 제스처의 이동 거리와 콘텐츠 이동을 검사해 짧은 탭만 분류한다. viewport 밖 입력, 드래그 후 원위치, 취소/다중 터치/늦은 ended, 페이지 교체 및 닫기 이후 입력은 프로필을 열지 않는다. 큐에서 WeakRef와 응답 세대를 확인한 뒤 기존 custom info popup을 연다. 우선순위 숫자나 전역 dispatcher 정책은 변경하지 않는다.
- 설정의 논리 높이 354는 iPad GD viewport보다 크다. init에서 main layer에 맞춘 scale은 네이티브 팝업 등장 시 1로 돌아가므로 상하단이 잘렸다. Popup의 실제 크기를 먼저 viewport에 맞추고, 그 안의 콘텐츠와 Popup 소유 버튼 메뉴에 같은 논리 좌표/scale을 적용한다. 바깥 main layer가 scale 1이 되어도 내부 적합 배율은 유지된다. 네이티브 탄성 확대를 끄고 설정을 높이 306으로 정리한다. Credits도 같은 fit 경로를 사용한다.
- Credits는 `기획`과 실제 이름, `음악`을 나눈다. 현재 설정 순서대로 같은 곡을 묶고 로비/라운드/결과 등 사용처를 한국어로 표시한다. 카드 한 줄은 기존 GD SongInfo의 실제 `작곡가 - 곡명`이다. 음악이나 기여자를 임의 생성하지 않고, 기존 요청 수 상한/타임아웃/닫힌 UI 검사/글로벌 delegate 비점유를 유지한다.

## 재현 테스트와 한계

`SurvivalBeta89NativeTest.mjs`의 시상식 transform/alpha 및 native scroll gesture 검사는 수정 전 beta.88에서 assertion 실패, 수정 후 통과한다. 설정의 init prefix와 실제 콘텐츠 배치를 실행하고 네이티브 등장 시 바깥 배율이 초기화되는 조건을 재현했을 때, 이전 코드는 화면 경계 assertion이 실패한다. 새 코드는 432×324, 480×335, 568×320, 640×480, 400×280에서 통과한다.

기존 beta.88의 순수 단상 timeline 테스트에는 CCLayer anchor/투명 fill 동작이 없었고, 메뉴 테스트에는 ScrollLayer의 began 수신이 없었다. 따라서 그 테스트가 통과해도 이번 렌더링/제스처 실패를 검출하지 못했다. 이번 회귀는 이 경계를 추가하며 실제 SDK 플랫폼 컴파일도 별도로 수행한다.

`LeaderboardNativeTest.mjs`는 실제 popup translation unit으로 탭→큐→정확한 계정 popup, 중복 ended, 세대 변경, busy 중 취소를 실행한다. 기존 custom 통계/봇 no-lookup, 결과 정산·observer·Pause·FREE PLAY·5초 hold/3초 GO·썸네일 캐시 및 서버 회귀를 유지한다. 서버 API/DB/LP 계산/네이티브 기록은 변경하지 않는다.

이 환경에는 실제 iPad/Windows GD 실행 화면이 없다. 테스트 더블/SDK 컴파일/패키지 검증을 실기기 확인이라고 보고하지 않는다. 추가 확인은 단상 전 단계 중앙 정렬과 검은 fill 부재, iPad 설정 하단 버튼/슬라이더/toggle/Credits, 긴 곡명 스크롤, 리더보드 카드 위 수직 드래그·짧은 탭·빠른 페이지 변경·뒤로가기다.

## 배포

버전은 beta.89로 한 단계 증가한다. Release/Debug는 보호된 GitHub 워크플로를 통해 새 immutable R2 경로에 게시한다. Railway 자동 배포에서 양쪽 신규 패키지가 공개되기 전에는 beta.88 접속 요구를 유지한다. 기존 R2 beta.88 파일, Railway 환경 변수 및 Neon 데이터는 변경하지 않는다. 실제 실행 수/플랫폼 결과/공개 파일 SHA/배포 commit은 작업 완료 보고에 기록한다.
