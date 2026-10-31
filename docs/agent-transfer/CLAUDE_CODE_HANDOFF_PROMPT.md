# Claude Code 인계 프롬프트

아래를 Claude Code에 붙여 넣으세요.

```text
Repository: ybaf100/Corum-Ranked

이전 Codex 세션을 이어받는다. 기존 구현을 반복하지 말고, 작업 직전에 최신 origin/main과 AGENTS.md를 확인한다.

인계 브랜치:
handoff/claude-code-20261031-beta122

1. 시작 상태 확인

git fetch origin
git switch handoff/claude-code-20261031-beta122

로컬 브랜치가 없으면:
git switch --track origin/handoff/claude-code-20261031-beta122

최초 handoff commit은 e1705a68171a60918fff00e82275abe145b3a813 이다. Notion/prompt 정리를 위해 이 브랜치가 이후 갱신될 수 있으므로 기대 SHA와 같지 않다고 reset하지 않는다. git status, HEAD, origin/main을 기록하고 변경을 보존한다.

2. 먼저 읽을 저장소 자료

- CLAUDE.md
- AGENTS.md
- docs/agent-transfer/AGENT_HANDOFF.md
- docs/agent-transfer/SESSION_TRANSCRIPT.md
- docs/agent-transfer/VERIFICATION.md
- docs/agent-transfer/DELIVERY.md
- docs/agent-transfer/evidence/CODEX_UNIFIED_PROMPT.md
- docs/agent-transfer/evidence/BETA122_COMPLETION.md
- ranked/docs/workflow-recapture-collision-beta122-diagnosis.md
- sha256sum --check docs/agent-transfer/FILE_SHA256SUMS.txt

Transcript는 완전한 대화 export가 아니다. 긴 사용자 요청 일부는 요약이며, 보이지 않는 대화나 이미지 내용을 창작하지 않는다. 첨부 prompt와 실제 채팅 원문을 혼동하지 않는다.

3. Notion을 작업 전 참고한다

Notion 연결/MCP가 사용 가능하면 먼저 Corum Ranked Canonical Index를 읽는다:
https://app.notion.com/p/3ce44344d8ba8114a916ea7f28eec697

Canonical context:
https://app.notion.com/p/3c844344d8ba81f990d3e0caff87d6d3

필요한 범위에 맞춰 관련 하위 페이지를 읽는다:

- Game Rules: https://app.notion.com/p/3ce44344d8ba81f6ba7acc8a682d7164
- Bugs / Regression / History: https://app.notion.com/p/3ce44344d8ba818d948dc4787b78670c
- Testing / CI / Release: https://app.notion.com/p/3ce44344d8ba811cad72e738f2800196
- Client / UI / UX: https://app.notion.com/p/3ce44344d8ba81c18b4ef00ad1f2b94e
- Server / DB / API: https://app.notion.com/p/3ce44344d8ba81d6a32af674dfb2f604
- Achievements / AP / Titles: https://app.notion.com/p/3da44344d8ba812b8447c6ba8c0e1614

Notion은 장기 의사결정과 프로젝트 맥락의 근거다. 각 페이지의 날짜/범위를 확인하고, 과거 상태를 현재 사실로 취급하지 않는다. 이 인계 시점에는 beta.30/11 등 과거 기록이 남아 있었고 beta.122 최신 기록을 관련 페이지에 덧붙였다. 새 작업에서는 최신 코드·GitHub CI·실제 배포를 직접 확인해 Notion과 불일치를 조정한다. Notion 접근이 안 되면 이를 짧게 알리고 저장소 증거로 계속한다.

4. 현재 기록된 상태와 검증 한계

이전 beta.122 source SHA:
a122cddce51fdf9c8d8a90c6f81d91b048dd8868

당시 Release/Debug 게시와 Singapore 운영 활성화는 완료됐지만, 과거 결과는 현재 상태를 보증하지 않는다. 실제 GD 2.2081/CBF 1.5.0 replay와 Entropie 이동 함수는 미확정이다. CBF 검증은 독립 Native doubles에서 수행했으며 기기 성공으로 말하지 않는다. 새 작업 전에 현재 main, Notion 최신 업데이트, Actions, 운영 상태를 각각 재확인한다.

5. 권한과 안전 규칙

이 단계의 목적이 문서 인계라면 코드 구현, 버전 증가, main 병합, 패키지 게시, 운영 배포를 하지 않는다. 후속 사용자가 범위를 새로 지시할 때만 해당 작업을 시작한다.

최신 main 위에서 정상 변경을 보존한다. reset, force push, 테스트 우회, 기록/Best/계정/LP/AP/업적 초기화는 금지한다. DESYNC 허용오차를 무작정 늘리거나 검사를 제거하거나 좌표를 덮어쓰거나 가짜 재생으로 숨기지 않는다. 증거와 가설을 구분하고, 수행하지 않은 테스트/배포를 완료라고 보고하지 않는다. 토큰/Secrets/인증자료를 출력하거나 문서에 넣지 않는다.

후속 구현이 승인되면 AGENTS.md의 Singapore Lightsail/R2 release gates를 적용한다. Seoul, Render, Railway에 배포하지 않는다.

먼저 읽은 자료와 확인한 SHA/branch/Notion 페이지를 짧게 보고한 뒤 지시된 범위만 진행한다.
```
