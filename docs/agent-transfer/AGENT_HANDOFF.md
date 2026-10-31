# Claude Code 인계 — 2026-10-31

## 현재 목표 및 권한

최신 요청은 **고충실도 인계 패키지 작성·전용 브랜치 커밋/Push**다. main 병합·운영 배포·신규 구현은 금지다. 이는 AGENTS.md의 일반적인 연속 배포 승인보다 우선하는 명시적 범위 제한이다. 완료 조건은 세 필수 문서, 규칙/증거/코드 보존, 원격 전용 브랜치 확인 및 한계 보고다. 전체 채팅 원문 export는 제공되지 않았고 이 패키지의 transcript는 부분 전사다. 완전 원문 인계는 아직 충족하지 못했다.

인계 시작: branch main, HEAD=origin/main `a122cddce51fdf9c8d8a90c6f81d91b048dd8868`, clean. `git fetch origin main` 성공 후 더 최신 main이 없음을 확인했다. 인계 브랜치 `handoff/claude-code-20261031-beta122`. 최종 문서 커밋 SHA는 자기 참조 문제로 본문에 넣지 않으며 `git rev-parse HEAD`와 최종 응답으로 확인한다. 이 브랜치의 부모에 기존 beta.122 코드 62개 파일 변경이 모두 포함된다. 초기 beta.122 임시 커밋들도 기존 로컬 브랜치에 보존돼 있다.

## 이전 구현 목표와 상태

최신 첨부 `Corum_Ranked_Workflow_Recapture_ReplayDesync_Codex_2026-10-31_v2.zip`의 A/B/C가 beta.122 작업이다. `evidence/CODEX_UNIFIED_PROMPT.md`는 첨부 원문 그대로다. 이를 채팅 원문으로 오인하지 말 것.

| 범위 | 상태 | 근거/한계 |
|---|---|---|
| A workflow 병렬화·안전한 게시/배포 | 구현/자동검사/실제 배포 완료 | 통합 run41177728737 attempt2 success, 두 R2 및 운영 SHA 일치 |
| C CBF 충돌 대상 신원·owned restart | 구현/자동검사 완료 | actual root/cursor + 독립 Native double280frames,3회 재시작 통과. 실제 GD/CBF 미검증 |
| B Entropie Native 증거 흐름 | 부분 완료, 진단 릴리스 | 사용자 B4 허용. 제한적 source→codec→server→viewer 추적 및 음성 fixture 추가. 정확한 Native 이동 함수·실기기 양성 fixture 미확정 |
| 실기기 회귀/실제 음질·화면 | 미완료 | 이 환경에 GD/CBF 앱·기기 실행 수단 없음 |
| 인계 원문 보존 | 부분 완료 | 전사 범위/긴 지시 요약/자동 export 부재를 transcript에 표시 |

beta.122: main/운영 SHA `a122cddce51fdf9c8d8a90c6f81d91b048dd8868`. 상세 성공/실패 수, 패키지 URL/해시, 운영 DB 측정, 시간 비교는 `evidence/BETA122_COMPLETION.md` 및 JSON 참조. 이는 2026-10-31 07:02 UTC경 검증 기록으로 현재 상태 보증이 아니다. 인계에서는 재배포하지 않는다.

## 반드시 보존할 사용자 정책

- 최신 정상 main을 기준으로 시작, fetch/AGENTS 재확인. 정상 커밋과 다른 변경을 보존. reset/force push/점수·계정·DB 초기화/테스트 우회 금지.
- 좌표 오차 확대, DESYNC 검사 제거, 좌표 덮어쓰기나 가짜 화면으로 재생을 성공 처리하지 않는다. 실기기와 doubles/빌드 성공을 구분한다.
- 원본 Native Random 선택 결과를 재적용; 임의 RNG 추정 금지. Survival 월드는 본인 clear replay + 선택 상대1명 Pose 정책 유지.
- 16/64인 축소·실제 R1부터 번호·Force Death Match·3분 서버 타이머·모든 Survival DM6회·마지막 Attempt 실제 종료/기록 처리 후 자동 복귀 유지. Solo 별도 BO3는 바꾸지 않는다.
- 뱃지는 서버 확정, 탈락 우선, 미진출 제외, bots 수집 제외, 영구 중복 방지. ELIMINATED는 난이도 획득 합계와 분리. FINAL과 DM1/DM2… 구분 및 반복 DM 표시.
- 전시장 난이도는 실제 맵 선정 공통 정책상 가능한 난이도만; 보유량/현 pool 존재 여부로 필터 금지. 5개 가로 viewport, ELIMINATED 마지막.
- 뱃지 얼굴/실제 등급 효과/정사각형·일정한 작은 얼굴, 상세 RATED→FEATURED→EPIC/LEGENDARY→MYTHIC의3+2 배치, dark panel+테두리 강조. 최신 지시는 결과/기록도 같은 얼굴 형식(그 이전 '결과 유지'를 대체). 난이도명 영어 허용. 난이도명 대신 보유 총수, 도전과제 뱃지 버튼 확대. Solo 소수1자리 표시 요구도 보존.
- 기존 replay/best, LP/MMR/AP/업적/ledger/칭호/맵풀/FreePlay/오디오 복구/기록을 손상하지 않는다. 새 뱃지에 LP/AP 보상 추가 금지.
- 일반 패치의 배포 승인은 유지되지만 **이번 인계 작업에는 적용하지 않는다**. 이후 구현을 사용자가 재개하면 기존 안전한 두 immutable 채널→Singapore 경로를 따른다. Seoul/Render/Railway 금지.
- 외부 라이선스 확인. CBF MIT attribution 유지, PolyForm Strict/GPL/무라이선스 소스를 직접 복사하지 않았다.

## 결정 근거·시도와 실패

1. raw lastCollision int 차이만 확정; 과거 정수의 정확한 Native 의미/같은 대상 여부는 미확정. 숫자를 지우지 않고 실제 후보+P1/P2별 방향 dictionary key+현재 object 동일성+순서 포함 map manifest를 증거로 요구한다. mapIndex/objectID 안정 참조와4방향 ROOT/cursor 검사를 유지한다. 중복/삭제/재생성/순서 변경/다른 대상은 fail closed.
2. 재시작 초기 원본 dictionary는 empty였는데 live stale log가 남음. owned Native reset 완료 뒤 검증된 empty dictionary만 복원하고 strict validation/전체 snapshot 복원을 수행한다. scalar IDs를 무조건 -1로 바꾸지 않는다. nonempty/unknown 원본은 추측 금지.
3. 기존 raw-ID replay의 증거 없는 변환은 불가능. 파일/Best를 보존하고 필요한 경계에서 COLLISION IDENTITY UNAVAILABLE로 보류한다. 파싱 호환과 실제 재생 지원을 구분한다.
4. Entropie viewer에 이동 증거 없음은 확정, source Native 함수/손실 지점은 불명. level/time/X 범위와 로그 예산을 제한해 source callbacks/capture/전송/저장/decode를 추적한다. 400-unit 검증 한도 유지, setPosition 관측 자체로 이동 승인하지 않는다.
5. Debug tests 뒤 직렬 build가 병목. 전체 gates는 게시/활성화에 유지하면서 빌드/이미지만 병렬 준비한다. 동일 parent의 upstream 고정 artifact명 충돌을 발견해 채널·플랫폼별로 분리, 같은 공식 CLI merge 사용.
6. 초기 개발 Native doubles의 새 진단 interface 누락·workflow 이동 후 기존 단언 실패는 수정 후 전체 재실행했다. 초기 로컬 상세 실패 로그는 scratch에만 남아 있으며 여기서 완전한 단계별 원문을 복원하지 않는다.
7. Actions attempt1은 runner step 전 실패. 사용자가 '워크플로 정상화함. 다시해' 후 failed jobs 재실행, attempt2 성공. 결제/runner 장애 원인 확정 못함.
8. macOS compiler cache의 upstream 경로가 unverified custom nightly를 요구해 미활성화. source cache만 사용. Windows/Android64 실측 hit16.19%; 완전 cold/warm 대조 실험은 없음.

## 다음 실행 순서 (신규 구현은 현재 보류)

P0: 이 브랜치 checkout 후 CLAUDE.md→AGENTS.md→이 문서→transcript→verification→첨부 원문 순서로 읽고 `git status`, HEAD, fetch 결과 확인. 최신 인계 제한 유지.

P0: 완전 원문 인계가 필요하면 사용자의 ChatGPT 대화 export를 받아 transcript의 요약 구간을 실제 원문으로 보충한다. 지금 없는 turn/시간/이미지 파일을 창작하지 않는다.

사용자가 재현/구현을 재개한 경우 P1: GD2.2081+CBF1.5.0 실제 환경에서 새 Source recording 생성. 네 원본 Attempt의 replay/best를 보존하며 새 witness 포함 기록을 별도로 테스트. 동일 replay3회 reset, Cube/Ship/P1/P2/dual/slopes/orb/portal/death/restart/Seek/0.5/1/2/4x를 Attempt 종료까지 비교. legacy raw-only 파일을 성공으로 강제 변환하지 않는다.

P1: Entropie143308401, source70–73초/X25800–26000, 독립 Attempt의71.295초를 재캡처. source callback→capture reason→QUEUED/ENCODED→server ACCEPTED/STORED/LOADED→viewer RECEIVED/DECODE_REJECT에서 최초 유실을 찾는다. 정확한 Native 함수와 실제 양성 fixture를 확보한 뒤 근본 수정한다.

P2: 실제 supported 플랫폼 UI/음질·Native regression. 새 증거/원인 없이는 추측성 추가 패치 금지. 단순 rerun이 필요하면 CI 실제 jobs/로그를 확인하고 현재 production SSH/rollback을 취소하지 않는다.

## 소스 지도

정확한 줄 위치는 `SOURCE_LOCATIONS.txt`(인계 시작 SHA 기준) 및 `rg`로 재확인한다.

- C: `corum-ranked-mod/src/replay/SurvivalReplayCollisionIdentity.hpp`; `SurvivalReplayCbfNative.cpp` (`CollisionWorld`, `captureCbfInitial`, `prepareCbfInitialCollisionLogs`, `NativeCollisionQuery`, root/cursor); `SurvivalReplayCbfTrace.hpp`, `SurvivalReplayNative.cpp` (`resetReplayInitial`), `SurvivalReplayPlayback.hpp`, `SurvivalSceneBridge.hpp`, `PlayLayer.cpp`.
- C tests: `corum-ranked-mod/tests/SurvivalReplayCbfNativeTest.mjs` MACOS/WINDOWS, independent Native doubles.
- B: `src/domain/SpectatorSourceTrace.hpp`, `SpectatorCapturePolicy.hpp`, `SpectatorRemotePolicy.hpp`, `spectator/SpectatorPoseCodec.hpp`, `SpectatorLiveRuntime.cpp`, `debug/DebugSpectateRuntime.cpp`; server `match/spectator-live.protocol.ts`, `debug-spectate/debug-spectate.service.ts`, `survival/survival-spectator-live.service.ts`; fixture `tests/fixtures/entropie-unproven-snaps-beta121.json`.
- A: `.github/workflows/ranked-release.yml`, `ranked-native.yml`, `ranked-feedback.yml`, `ranked-ci.yml`, `build-ranked-mod.yml`, `build-ranked-debug.yml`, `deploy-ranked-lightsail.yml`; `ranked/tools/verify-published-clients.py`, `verify-production-release.py`; `corum-ranked-mod/tools/combine-ranked-packages.py`, `prepare-mod-release.py`, `publish-mod-r2.py`.
- 상세 설계: `ranked/docs/workflow-recapture-collision-beta122-diagnosis.md`. 전체62개 변경 파일은 첨부 completion JSON `changedFiles`.

## Claude 환경 이전

root CLAUDE.md가 명시적으로 AGENTS.md와 이 패키지를 읽도록 안내한다. Codex 전용 tool 이름·연결은 Claude에 자동 이전되지 않는다. 본인 GitHub/Neon/SSH 권한을 안전하게 재설정하되 토큰·Secrets를 문서나 채팅에 붙이지 않는다. 환경 파일을 복사/출력하지 않는다. 이 패키지에는 인증정보가 없다. `ATTACHMENTS.json`은 원본 파일 경로/크기/해시 목록이며 로컬 경로는 새 환경에서 유효하지 않을 수 있다. raw Geode 로그와 inline 이미지 바이너리는 저장소에 넣지 않았다. 별도 사용자 제공 첨부가 필요하다.
