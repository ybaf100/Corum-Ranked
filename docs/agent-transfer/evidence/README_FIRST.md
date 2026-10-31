# Corum Ranked Codex 일괄 작업 패키지

1. `CODEX_UNIFIED_PROMPT.md`를 Codex의 작업 지침으로 사용한다.
2. `LOG_ANALYSIS.md`와 `WORKFLOW_BASELINE.md`로 재현 및 성능 측정값을 확인한다.
3. `logs/primary`의 최신 `Geode 2026-10-31 02.16.39.log`와 Entropie 로그 3개를 우선 읽는다. 새 로그의 `CBF_ROOT:before` 5건 및 초기화 실패 2건을 `REPLAY_DESYNC_CASES.json`과 대조한다.
4. `logs/context/spectator`는 같은 오류의 선행/비발생 테스트 비교, `logs/context/replay`는 기존 Replay 회귀 방지용으로 사용한다.
5. `references/PREVIOUS_REPLAY_FIX_PROMPT.md`는 이전 작업 지침 참조다. 이번 추가 구현 대상은 **CBF 충돌 상태 ID 및 재시작 오류**이며 이전 전체 Replay 기능의 무조건적 재작성은 금지한다.
6. Remote main을 다시 fetch한다. 패키지에 작성된 SHA/버전은 조사 시점 스냅샷이다.
7. 실기기 검증을 수행하지 않았다면 성공이라고 보고하지 않는다.

포함 원본 Geode 로그: 총 11개. 원본 내용은 수정하지 않았다.

프로젝트: `ybaf100/Corum-Ranked`.
작업 범위: GitHub Actions 병목 구조 개선 + Entropie `RECAPTURE REQUIRED` 실제 이벤트 캡처 및 관전자 검증 개선 + CBF Replay의 충돌 ID 불일치 및 재시작 충돌 로그 복원 수정. 이전 Replay의 Attempt 종료 저장 기능은 회귀 검증한다.

주의: 새 Replay 로그에는 실제 물리 상태 배열과 충돌 정수 불일치가 직접 기록되어 있지만, 해당 정수가 원본/재생에서 같은 맵 오브젝트를 가리킨다는 결론은 미확정이다. 값 비교를 무력화하는 수정을 허용하지 않는다.
