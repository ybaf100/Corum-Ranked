# Corum Ranked v0.5.0-beta.2

## **밑 Assets 항목을 펼치면 모드를 다운로드할 수 있습니다. `.geode`로 끝나는 파일을 받으세요.**

- iOS에서 LevelInfo Play selector가 실행되지 않아 반복되던 `STARTING -> FAILED`를 수정했습니다.
- Round 맵을 열던 중 Death Match 등 새 맵 컨텍스트가 도착하면 기존 LevelInfo 창을 즉시 정리해 두 시작 흐름이 겹치지 않도록 했습니다.
- `CCParticleFire` iOS 헤더 경로와 `RatingEventView` 네임스페이스 빌드 핫픽스를 포함했습니다.
- 기존 `Build Corum Ranked Mod` 수동 실행에 Debug Bot 빌드 옵션을 통합했습니다. 정식 push/PR 빌드는 계속 Debug Bot 코드를 제외합니다.
- 클라이언트·서버 필수 버전을 `v0.5.0-beta.2`로 맞췄습니다.

beta.1에서 데이터베이스 마이그레이션이나 Apps Script 변경은 필요하지 않습니다. 서버와 클라이언트는 반드시 함께 배포해야 합니다.
