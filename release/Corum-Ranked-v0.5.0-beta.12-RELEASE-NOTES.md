# Corum Ranked v0.5.0-beta.12

## 변경 사항

- 비허용 활성 모드를 감지하면 raw 오류 코드 대신 Ranked 전용 게임 내 경고창을 표시합니다.
- 경고창은 설치된 모드의 표시명과 ID를 함께 보여주고, 비허용 모드·필수 모드 누락 또는 비활성·버전 불일치·CBF 설정 문제를 구분합니다.
- `다시 검사` 버튼으로 현재 모드 환경을 다시 확인할 수 있습니다.
- Node IDs `v1.23.3` 이상을 필수 Geode dependency와 서버 `Ranked Allowed Mods` 기본값에 추가했습니다.

## 운영 반영 주의

- Apps Script를 최신 버전으로 반영하고 `setupCorumRankedConfig()`를 실행해 `geode.node-ids` 행을 추가해야 합니다.
- `Ranked Allowed Mods`에서 `geode.node-ids`가 Enabled/Required이며 최소 버전이 `v1.23.3`인지 확인해야 합니다.
- 서버와 클라이언트는 정확한 `v0.5.0-beta.12` 버전 게이트를 함께 사용합니다.

## 검증 상태

- Apps Script validators: 통과
- Rules/Server TypeScript typecheck: 통과
- Rules/Server tests: 통과
- Server build: 통과
- Windows/iOS/Android Geode CI: 필요
- 실제 게임 내 한글 경고창 및 모드 재검사: Device verification 필요
