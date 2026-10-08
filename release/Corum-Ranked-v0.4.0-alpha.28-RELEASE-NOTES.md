Corum Ranked (v0.4.0-alpha.28)

## **밑 Assets 항목을 펼치면 모드를 다운로드 할 수 있습니다 .geode로 끝나는 파일을 다운하세요**

# v0.4.0-alpha.28
- 마지막 10초 안에 시작한 Attempt가 30초 intent 만료/120초 orphan timeout 때문에 중간 강제 종료되던 경로 제거
- Final/Last Attempt는 시작 제한시간만 지키면 사망 또는 Clear할 때까지 자연스럽게 계속 플레이하도록 서버/클라이언트 양쪽 보강
- 빠른 마지막 Attempt 여러 개의 start-intent가 네트워크 요청 하나에 덮이지 않도록 intent 전송 큐와 재시도 추가
- 서버 결과가 잘못 먼저 보여도 살아 있는 Attempt를 즉시 `onQuit()`하지 않도록 안전장치 추가
- 결과 확정 뒤 `SYNCING RESULT` 화면에서 영구 대기하던 문제 제거
- 결과/Settling 이후 vanilla reset으로 가짜 다음 Attempt가 시작되는 경로 추가 차단
- Neon pooled DB 연결이 끊겨도 Render Node 프로세스 전체가 종료되지 않도록 PostgreSQL Pool 오류 처리 추가
