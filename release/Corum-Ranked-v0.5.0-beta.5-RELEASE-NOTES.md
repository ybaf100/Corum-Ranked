# Corum Ranked v0.5.0-beta.5

## 변경 사항

- Ranked 로비, 큐, Match Found, 밴픽, 라운드 준비, 관전, 라운드 결과, 최종 결과와 히스토리를 하나의 경쟁형 UI 디자인으로 전면 개편했습니다.
- 사용자 제공 Paperlogy Regular·Medium·SemiBold·ExtraBold·Black 전 굵기를 포함했습니다. 일반 UI 문구는 SemiBold를 기본으로, 핵심 수치·타이머·LP·티어·승패는 Black으로 분리했습니다.
- 게임 HUD의 큰 좌우·중앙·하단 배경 패널과 글자별 사각 스크림을 모두 제거했습니다. 각 글자에만 밀착된 검은 그림자를 사용해 밝은 맵에서도 읽히면서 지형을 가리지 않습니다.
- 게임 HUD는 편집 시안 비율에 맞춰 이름, 점수, Clear, 라운드, 타이머와 Qualifying을 다시 확대하고 간격을 압축했습니다.
- Ranked 로비, Match Found, 밴픽, 준비, 관전, 결과와 히스토리의 주요 텍스트를 확대해 빈 공간을 줄였고, 연패 표시는 빨간 Black 텍스트와 구분선으로 강화했습니다.
- 디버그 빌드 Ranked 로비 우측 하단에 한글 `디버그` 글리프 검증 문구를 추가했습니다.
- `PERFECT WIN` 결과에 금색 전용 배너와 최대 보상 안내를 추가했습니다.
- 티어 트로피는 아직 미포함이며, 지정된 파일을 추가하면 자동 적용됩니다. 기존 반복 불꽃 PNG는 제거하고 텍스트를 가리지 않는 절제된 빛 레일 연출로 교체했습니다.
- 실제 Play 버튼과 맵 업데이트 버튼을 기본 스프라이트로 검증하고, 저장된 레벨의 타입과 Geometry Dash의 기존 Play 콜백을 보존하도록 Play/update 반복 루프 핫픽스를 병합했습니다.
- 맵 업데이트가 진행 중일 때 버튼을 반복 활성화하지 않고 vanilla busy 상태가 끝날 때까지 기다린 뒤 자동 재시도합니다.
- beta.4의 서버 권위 경기 규칙, Attempt, MMR·LP·Perfect Win 계산은 변경하지 않았습니다.

## 커스텀 이미지

빌드 전에 `corum-ranked-mod/resources/`에 문서에 지정된 PNG 파일을 넣으면 티어 트로피가 자동 적용됩니다. 파일이 없으면 크기를 줄인 Geometry Dash 기본 별 리소스로 안전하게 대체됩니다.

## 배포

추가 DB migration이나 Apps Script 설정 변경은 없습니다. beta.5 서버와 정확히 일치하는 beta.5 클라이언트를 함께 배포합니다. GitHub Actions 빌드에는 갱신된 `.github/workflows/build-ranked-mod.yml`도 반드시 반영해야 합니다.
