# 인계 전달 및 사용

전용 브랜치: `handoff/claude-code-20261031-beta122`.
코드 기준/부모: `a122cddce51fdf9c8d8a90c6f81d91b048dd8868`.
이 문서를 포함한 최종 커밋 SHA는 최종 사용자 응답 또는 `git rev-parse HEAD`로 확인한다.

```sh
git fetch origin
git switch --track origin/handoff/claude-code-20261031-beta122
git status --short --branch
git rev-parse HEAD
cat CLAUDE.md
cat AGENTS.md
```

이미 같은 로컬 브랜치가 있으면 switch만 사용하고 강제로 덮어쓰지 않는다.

Claude Code 시작 메시지:

> CLAUDE.md, AGENTS.md, docs/agent-transfer/AGENT_HANDOFF.md,
> SESSION_TRANSCRIPT.md, VERIFICATION.md를 읽고 현재 상태를 확인하라.
> 이번 인계 단계에서는 신규 구현, main 병합, 패키지 게시, 운영 배포를 하지 말라.
> 기록된 성공은 과거 자동 검증 범위이며 GD/CBF 실기기 성공을 뜻하지 않는다.
> transcript의 요약 구간을 원문으로 오인하거나 없는 대화를 만들어내지 말라.

## 파일 목록

- root CLAUDE.md: 자동 시작용 규칙 연결.
- CLAUDE_CODE_HANDOFF_PROMPT.md: Notion 참고를 포함한 Claude용 복사 프롬프트.
- AGENT_HANDOFF.md: 목표·권한·상태·결정·가설·다음 작업.
- SESSION_TRANSCRIPT.md: 순서별 부분 전사와 명시적 요약, 한계.
- VERIFICATION.md: 실제 검사/명령·오류·미검증·환경.
- SOURCE_LOCATIONS.txt: 기준 SHA의 함수별 줄 위치.
- ATTACHMENTS.json: 원본 첨부 경로/크기/SHA256; 바이너리는 미포함.
- evidence/: 최신 첨부의 원본 prompt/분석/cases/index/checksums 및 이전 배포 완료 보고/구조화 결과.
- FILE_SHA256SUMS.txt: 이 파일과 다른 인계 파일의 무결성(자기 자신 제외).

## Notion 참고 위치

Claude Code prompt는 Notion Canonical Index와 Canonical Context를 먼저 읽고, 작업 범위에 맞게 Game Rules/Bugs/CI-Release/Client/Server/Achievements 페이지를 선택하도록 안내한다. 각 항목의 날짜를 확인하고 최신 main/CI/운영 증거와 대조한다. Notion 연결이 없으면 그 한계를 알리고 저장소를 기준으로 이어간다.

- [Corum Ranked Canonical Index](https://app.notion.com/p/3ce44344d8ba8114a916ea7f28eec697)
- [Corum Ranked Canonical Context](https://app.notion.com/p/3c844344d8ba81f990d3e0caff87d6d3)
- 상세 page URLs는 `CLAUDE_CODE_HANDOFF_PROMPT.md`에 있다.

## 한계

최초 긴 사용자 메시지4개는 전체 원문 전사 대신 요약이다. 완전한 대화 원문 인계 조건은 미완료이며 사용자 export로 보충해야 한다. 압축 이전 assistant/tool turns, 실제 message timestamps, inline image bytes는 접근할 수 없었다. 첨부 Geode 로그/ZIP 파일 자체는 별도 원본으로 가져와야 하며 이 패키지의 attachment SHA와 대조한다. evidence/SHA256SUMS.txt는 원본 ZIP 추출 전체를 위한 것이므로 logs 없이 이 디렉터리에서 전체 검증하면 누락이 나는 것이 정상이다. 인계 패키지는 별도 FILE_SHA256SUMS.txt로 검증한다.

기존 코드 변경은 부모 커밋에 전부 존재한다. 이번 커밋은 문서만 추가한다. main이나 운영에 반영하지 않는다. 단순 secret-pattern scan은 알려진 credential 패턴 부재를 확인한 것이며 완전한 보안 감사는 아니다. 원본 로그/CI 출력/환경 파일/인증정보는 포함하지 않았다.

Push 실패 시 대안: 현재 commit에서 `git bundle create claude-handoff.bundle HEAD`로 모든 reachable 코드 history와 문서를 전달하고 `git bundle verify`로 확인한다. 전달 후 clone하거나 fetch한다. 실제 원격 확인 결과는 최종 응답에 보고한다.
