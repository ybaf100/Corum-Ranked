# Corum Ranked v0.5.0-beta.3

## **밑 Assets 항목을 펼치면 모드를 다운로드할 수 있습니다. `.geode`로 끝나는 파일을 받으세요.**

- 게임 시작은 직접 씬 전환이 아니라 화면의 실제 Geometry Dash Play 버튼을 자동으로 누르는 방식으로 수정했습니다.
- 대기 중에는 노래 다운로드·취소·정보 컨트롤만 작동하며, 수동 Play 우회는 제공하지 않습니다.
- 게임 도중 맵 선택창으로 나가면 서버가 다음 시도를 허용하는 동안 실제 Play 버튼을 다시 자동으로 누릅니다. 시간 종료·결과 처리·관전·3-Clear·Deathmatch 3회 제한에서는 재진입하지 않습니다.
- `Ranked Tiers`의 `Main Pool` 값이 실제 5개 후보 맵 분포를 결정합니다.
- `CSMP Tiers`의 새 이름을 `Ranked CSMP Seed`에서 동적으로 사용할 수 있습니다. 기존 계정은 자동 재Seed되지 않습니다.
- 클라이언트·서버 필수 버전을 `v0.5.0-beta.3`로 맞췄습니다.

배포할 때 Apps Script를 갱신하고 `setupCorumRankedConfig()`를 실행한 뒤, 서버보다 먼저 `ranked/migrations/0006_v050_dynamic_csmp_tiers.sql`을 적용하세요. 서버와 클라이언트는 반드시 함께 배포해야 합니다.
