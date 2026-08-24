# Corum Ranked v0.5.0-beta.5

## 변경 사항

- Ranked 로비, 큐, Match Found, 밴픽, 라운드 준비, 관전, 라운드 결과, 최종 결과와 히스토리를 하나의 경쟁형 UI 디자인으로 전면 개편했습니다.
- Ranked 전용 Corum Sans 폰트를 포함해 게임 HUD와 LevelInfo 대기창까지 동일한 타이포그래피를 적용했습니다.
- 게임 HUD를 좌우 플레이어 패널과 중앙 라운드·서버 타이머 구조로 재배치했습니다.
- `PERFECT WIN` 결과에 금색 전용 배너와 최대 보상 안내를 추가했습니다.
- 사용자가 직접 준비한 티어 트로피와 승급·PERFECT WIN 불꽃 텍스처를 파일 교체만으로 적용할 수 있습니다.
- 실제 Play 버튼과 맵 업데이트 버튼을 기본 스프라이트로 검증하고, 저장된 레벨의 타입과 Geometry Dash의 기존 Play 콜백을 보존하도록 Play/update 반복 루프 핫픽스를 병합했습니다.
- 맵 업데이트가 진행 중일 때 버튼을 반복 활성화하지 않고 vanilla busy 상태가 끝날 때까지 기다린 뒤 자동 재시도합니다.
- beta.4의 서버 권위 경기 규칙, Attempt, MMR·LP·Perfect Win 계산은 변경하지 않았습니다.

## 커스텀 이미지

빌드 전에 `corum-ranked-mod/resources/`에 문서에 지정된 PNG 파일을 넣으면 티어 트로피와 불꽃 텍스처가 자동 적용됩니다. 파일이 없으면 Geometry Dash 기본 리소스로 안전하게 대체됩니다.

## 배포

추가 DB migration이나 Apps Script 설정 변경은 없습니다. beta.5 서버와 정확히 일치하는 beta.5 클라이언트를 함께 배포합니다. GitHub Actions 빌드에는 갱신된 `.github/workflows/build-ranked-mod.yml`도 반드시 반영해야 합니다.
