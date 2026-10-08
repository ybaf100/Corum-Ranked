# Corum Ranked v0.4.0-alpha.17

## **밑 Assets 항목을 펼치면 모드를 다운로드 할 수 있습니다 .geode로 끝나는 파일을 다운하세요**

# v0.4.0-alpha.17
- Song 다운로드를 숨겨진 downloader 호출 방식에서 실제 Geometry Dash LevelInfoLayer 다운로드 버튼 방식으로 변경
- Song 다운로드 화면에서는 나머지 LevelInfo UI를 가리고 입력을 차단하여 vanilla song download control만 사용 가능
- 준비 카운트다운과 20초 song timeout을 실제 LevelInfoLayer에서 처리
- 20초 timeout 이후에도 정상 LevelInfoLayer `onPlay()` 경로로 노래 없이 시작하도록 수정
- alpha.16의 Ban 확정 처리 및 난이도별 색상 수정 유지
