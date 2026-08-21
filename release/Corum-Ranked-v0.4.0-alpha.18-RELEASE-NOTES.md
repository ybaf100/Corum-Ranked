# Corum Ranked v0.4.0-alpha.18

## **밑 Assets 항목을 펼치면 모드를 다운로드 할 수 있습니다 .geode로 끝나는 파일을 다운하세요**

# v0.4.0-alpha.18
- Qualifying 이상 진행했는데 점수가 0으로 남거나 Clear가 집계되지 않을 수 있던 클라이언트 Attempt 이벤트 유실 경로 수정
- Attempt Start를 PlayLayer에서 로컬 FIFO에 등록될 때까지 재시도하도록 변경
- Attempt End가 실제 큐에 들어가기 전에 완료 처리 플래그를 먼저 세우던 문제 수정
- `/attempt/start` ACK 대기 중의 진행률을 버리지 않고 보존 후 전송하도록 수정
- 게임 진입 직전 선택 맵/Qualifying snapshot을 고정하여 scoring context 안정화
- progress telemetry가 현재 poll map이 아닌 실제 승인된 attempt level ID를 사용하도록 수정
- Qualifying 이상 non-Clear 점수 저장 후 Clear 점수 누적을 검증하는 서버 integration regression 추가
- alpha.17의 실제 Geometry Dash Song 다운로드 버튼 방식 유지
- Corum Integration / Apps Script / DB schema 변경 없음
