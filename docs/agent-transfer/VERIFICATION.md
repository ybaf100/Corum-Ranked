# 검증 인계

## 범위와 시점

이번 인계에서는 신규 기능 구현·제품 테스트·배포를 실행하지 않는다. 아래 beta.122 결과는 이전 실행의 보존 자료다. 날짜2026-10-31 UTC, sourceSHA a122cddce51fdf9c8d8a90c6f81d91b048dd8868. 마지막 기존 공개 API 확인07:02:46. 인계 시 현재 main fetch만 다시 수행했다. 코드 위치는 SOURCE_LOCATIONS.txt, 전체 파일/결과는 evidence/beta122-completion-report.json.

## 인계에서 실제 실행한 검사

- `git status --short --branch`: 시작 main...origin/main, clean.
- `git fetch origin main`: 성공. `git log -1 origin/main`: a122... beta.122.
- `cat AGENTS.md`: deployment 기본값과 최신 명시적 제한 우선 적용 확인.
- `rg --files -g AGENTS.md -g CLAUDE.md`: root AGENTS만 존재, 신규 root CLAUDE 진입 지침 추가.
- `git switch -c handoff/claude-code-20261031-beta122`: 성공.
- `node --version`: v24.19.0; `npm --version`:11.9.0; `python3 --version`:3.12.14; `g++ --version`:Debian14.2.0.
- 최종 whitespace/credential pattern/파일 checksum/브랜치 Push 검사는 아래 DELIVERY.md 및 최종 응답 참조. 제품 tests를 인계 때문에 재실행했다고 보고하지 않는다.

## beta.122 실행 명령과 결과 (이전 기록)

| 명령/경로 | 결과 |
|---|---|
| `npm test --prefix ranked` 내부 rules workspace |361pass |
| `npm test --prefix ranked` 내부 server workspace |710pass,선택pool-reconnect1skip |
| `node --test --test-concurrency=2 corum-ranked-mod/tests/*.mjs` |전체329pass,skip0; 정확한 CI 명령은 ranked-native.yml 유지 |
| ranked-native.yml strict g++ domain compile/run 각10개 |10pass |
| `npm run typecheck`, `npm run build` (ranked/) |pass |
| `npm --workspace @corum-ranked/server test -- test/survival-online.integration.test.ts test/pool-reconnect.integration.test.ts` (격리 로컬 PG 설정) |67pass; production 연결 종료 검사 금지 |
| `python3 -m unittest discover -s ranked/tools/tests` 중 release/publication/production guards |대상15pass |
| ModCombineBuildTest.py / ModReleaseBuildTest.py / ModReleasePublishTest.py |4/2/10pass |
| `ranked/tools/check-workflows.sh` |pinned-checksum actionlint1.7.7 pass |
| `python3 ranked/tools/verify-production-release.py --expected-sha a122cddce51fdf9c8d8a90c6f81d91b048dd8868` |공개 R2 bytes 및 health/ready/config 일치pass |
| `python3 corum-ranked-mod/tests/verify-visibility-package.py <downloaded.geode> v0.5.0-beta.122 [--debug]` |양 채널5플랫폼 Native guards pass |
| `python3 corum-ranked-mod/tests/verify-font-package.py <downloaded.geode>` |각18font bundles pass |
| `python3 corum-ranked-mod/tests/verify-asset-package.py --mod <downloaded.geode>` |양 채널 외부 이미지 분리pass |

명령 기록은 결과를 재현하도록 정리한 것이며 누락된 shell transcript 원문을 창작한 것이 아니다. 실제 이전 전체 로그는 /workspace/scratch/beta122-native-all3.log, beta122-server-all2.log, beta122-postgres-integration.log 및 beta122-job-*.log에 있다(새 머신에는 없을 수 있음). 비밀/개인정보 노출 위험 때문에 raw CI/Geode 로그를 일괄 Git에 올리지 않았다. 정제된 통계/오류만 보존했다.

## 실패와 오류 원문 발췌/사실

- 원본 CBF ROOT before collision bottom:2166→2599,109→123,166→170,213→214.7failures/4Attempts,ROOT5+restart2. 상세 case JSON은 evidence/. 정수 의미 및 실제 동일 대상 여부는 가설로 남음.
- `CBF_INITIAL_COLLISION_LOG_MISMATCH`: 재시작 stale dictionary. 원본 초기 dictionary count0/scalar-1. 수정 후 독립 Native doubles3회 reset pass; 실제 Native3회 reset은 미검증.
- `CBF_COLLISION_TARGET_MISMATCH`: 잘못된 실제 대상 음성 테스트의 의도된 거부. 제품검사 실패를 숨긴 것이 아님.
- `CBF_LEGACY_COLLISION_IDENTITY_UNAVAILABLE`: 증거 없는 legacy 파일의 의도된 안전 보류. Best삭제 안함.
- Entropie 두 독립 Source 이동1395/1468.7, viewer event NONE. 동일 packet 반복 로그를 별도 source로 세지 않음. 실제 함수 미확정.
- Actions41177728737 attempt1: runner step 전 failure, 외부 원인 확정불가. 사용자 정상화 후 attempt2 success. 상세 run/공개 배포 증거는 completion 보고서.
- 이전 개발 중 doubles interface/이동된 workflow 단언 실패를 수정하고 최종 전체 suite를 통과했다. 세부 최초 실패 원문은 보존된 local logs만 신뢰한다.
- 이전 공개파일 직접 다운로드 첫 시도는 User-Agent 없는 urllib403; 기존 verifier와 동일한 User-Agent로 실제 bytes 다운로드/검증 성공. 운영 package 오류라고 오인하지 않는다.

## 재현 준비 및 남은 검사

1. 실제 GD2.2081+Geode SDK호환+CBF1.5.0 환경에서 새 recording 생성. input/native clocks/holding/jumpbuffer/grounding/velocity/rotation/RNG/actual target을 비교. CBFON/OFF를 분리. legacy파일 증거 부족은 명시.
2. Cube/Ship/dual/portal/slopes/orb/death/restart/Seek/0.5/1/2/4x를 완전Attempt끝까지; 같은 replay3회 restart. doubles280frames 통과를 실제 gameplay 성공으로 보고하지 않음.
3. Entropie143308401 70–73초/X25800–26000,71.295초 독립Attempt trace의 capture→codec→relay→viewer correlation. 정상 Native 증거가 없다면 원래 pose gate 유지, 긍정fixture 조작금지.
4. 실제 iOS/Android32/64/Windows/macOS 앱, 오디오 청취/HUD/뱃지 화면 미검증. 서버API/LP/AP는 자동 검사와 DB 측정 범위만 검증.
5. 실제 rollback 장애를 운영에 주입하지 않았다. 경로와 guards 유지. 완전 cold/warm 캐시 대조 미실행.

## 필수 환경/설정

Node24/npm11,Python3,g++C++23 strict,PostgreSQL16 로컬 격리. production Neon18은 기존 운영 writer이며 테스트 대상으로 사용금지. GeodeSDK5.8.2,CLI3.9.0,bindings70986055f526da15318779703f0e904be822f162,buildera0f7676e6fcd1a584f6d0c1c800239008412defc. package-lock.json/워크플로를 실제 의존성 권위로 사용한다.

Singapore HTTPS host는 AGENTS.md 참조. SSH known-host pinning 유지. R2 gates/환경 secrets를 우회하지 않는다. 인증자료는 안전한 별도 환경에서 구성하고 이 문서로 이전하지 않는다. Claude Code의 도구/권한은 재설정 필요하며 Codex connector가 자동 제공된다고 가정하지 않는다.

## 인계 최종 체크리스트

## 2026-10-31 · Notion 최신화 및 Claude prompt

- Notion은 workspace search로 기존 Corum Ranked Canonical Index/Context 구조를 찾아 쓰기 전 해당 페이지를 전부 fetch했다. Canonical 문서에 과거 beta.11/ybadfg/Render 상태가 남아 있어 과거 본문을 삭제하지 않고 2026-10-31 beta.122 authoritative override를 append했다.
- 업데이트 페이지 8개: Canonical Index, Canonical Context, Bugs / Regression / History, Testing / CI / Release, Client / UI / UX, Game Rules, Server / DB / API, Achievements / AP / Titles. 기존 Placement/Config 페이지는 beta.122 관련 제품 변경이 없어 그대로 뒀다.
- Notion markdown specification을 먼저 fetch하고 append-only insertion을 사용했다. 결과 페이지를 다시 읽어 beta.122 section/source SHA와 문서별 내용이 반영됨을 확인했다. 최종 Notion page URLs는 아래 및 Claude prompt 파일에 기록한다.
- credential/secret은 Notion이나 저장소에 추가하지 않았다. Notion update가 제품 코드나 DB를 변경하지 않았다.
- 사용자가 Claude prompt가 Notion을 참고하도록 요청해 `docs/agent-transfer/CLAUDE_CODE_HANDOFF_PROMPT.md`와 root `CLAUDE.md`를 갱신했다. Claude는 canonical index/context부터 읽고 작업 범위에 맞는 하위 문서를 선택하며 current main/CI/운영과 reconcile하도록 안내한다.
- 이번 문서 변경에 별도 product test를 추가/실행하지 않았다. 문서 whitespace 및 handoff checksum 검증 후 전용 branch에 push한다.

Notion pages:

- https://app.notion.com/p/3ce44344d8ba8114a916ea7f28eec697
- https://app.notion.com/p/3c844344d8ba81f990d3e0caff87d6d3
- https://app.notion.com/p/3ce44344d8ba818d948dc4787b78670c
- https://app.notion.com/p/3ce44344d8ba811cad72e738f2800196
- https://app.notion.com/p/3ce44344d8ba81c18b4ef00ad1f2b94e
- https://app.notion.com/p/3ce44344d8ba81f6ba7acc8a682d7164
- https://app.notion.com/p/3ce44344d8ba81d6a32af674dfb2f604
- https://app.notion.com/p/3da44344d8ba812b8447c6ba8c0e1614

- [x] code snapshot은 beta.122 전체 커밋 위에 보존, 기능 변경 없음.
- [x] 현재 목표/금지/상태/결정/실패/가설/다음작업/소스 위치/환경 기록.
- [x] 사용자 지시 순서와 최신 변경 우선순위, 실제GD 미검증 표시.
- [x] 직접 원문과 요약 분리, 첨부 prompt 원본 별도.
- [ ] 긴 사용자 메시지4개의 전체 원문 전사(현재 요약). ChatGPT export로 보충 필요.
- [ ] 초기 압축 이전 assistant/tool 원문 및 inline 이미지 복원(접근 불가).
- 원격 전용 branch/commit/clean 여부는 DELIVERY.md와 최종 응답에서 확인. main merge/배포 금지는 계속 적용.
