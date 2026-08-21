# v0.4.0-alpha.10

- Ranked UI를 팝업 방식에서 게임 화면 전체를 사용하는 in-game UI로 변경
- 제공된 UI 시안의 주황색 텍스트/화살표는 설명용 주석으로만 처리하며 실제 게임에는 표시하지 않음
- Match Found / Ban Map / Round / Death Match / Match End / Queue Again / Match History 상세 UI 추가
- 맵/노래 다운로드 버튼 및 Downloading/Downloaded 상태 추가
- 시작 5초 전까지 누르지 않은 다운로드는 자동 시작
- 맵 다운로드 최대 30초: 1라운드 실패는 Match Canceled(무효), 이후 실패는 해당 플레이어 Match Loss
- 노래 다운로드 최대 20초: 미완료여도 경기 시작, Geometry Dash 백그라운드 다운로드는 계속 진행
- 상대 리소스 대기 시 `WAITING FOR <PLAYER>'S DOWNLOAD...` 표시
- 2-Clear 규칙 변경: 상대가 0 Clear여도 10초 LAST ATTEMPT 시작창 부여, 창 안에 시작한 후속 Attempt까지 끝까지 반영
- 서버 판정으로 라운드/경기가 끝나면 Geometry Dash의 정상 quit 경로를 사용해 자동 퇴장
- 경기 기록에서 라운드별 점수와 Clear 횟수, Deathmatch 세부 결과 확인 가능
- Corum Integration 변경 없음
