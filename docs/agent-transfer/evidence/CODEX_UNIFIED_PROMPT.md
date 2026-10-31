# CODEX UNIFIED TASK: Corum Ranked CI/CD speed + Entropie RECAPTURE REQUIRED + CBF collision DESYNC

## 0. 목표, 기준, 작업 순서

Repository: `ybaf100/Corum-Ranked`
Last verified remote `main`: `5875c91cc3bf64b3eb1cfc3c019dcc5ef4f16b68`
Last verified client version: `v0.5.0-beta.121`

이 ZIP의 `README_FIRST.md`, `LOG_ANALYSIS.md`, `WORKFLOW_BASELINE.md`, `LOG_INDEX.json`을 읽는다. `logs/primary`를 직접 분석하고 `logs/context`는 필요할 때 비교한다. GitHub remote `main`을 작업 직전에 다시 fetch하고 최신 정상 코드를 기준으로 변경한다. 기존 정상 수정 사항을 reset하거나 덮어쓰지 않는다.

이번 작업은 세 개의 독립된 문제를 하나의 검증된 변경 묶음에서 해결한다.
A. GitHub Actions 병목을 줄여 실제 코드 수정 후 유효한 빌드와 운영 배포까지의 경과 시간을 단축한다.
B. Geometry Dash Entropie의 반복적인 `RECAPTURE REQUIRED` 원인을 증명하고, 검증 가능한 불연속 이동을 원본 캡처부터 관전자 재생까지 정확히 전달한다.
C. CBF 1.5.0 사용 시 Replay의 충돌 상태 ID 불일치와 재시작 충돌 로그 초기화 실패를 근본 해결한다.

기존 Survival Replay의 입력, DESYNC 감지, 비정상 종료 저장/복구를 손상시키지 않는다. Replay 충돌 상태 불일치를 이번 작업에 포함하되, 이전 모든 Replay 개선을 무제한으로 재구현하지 않는다. 과거 수정 지침은 `references/PREVIOUS_REPLAY_FIX_PROMPT.md`에 있다. Replay 물리 검증을 제거하거나 우회하는 수정을 금지한다.

## A. CI/CD 실제 병목 분석

관련 파일:
- `.github/workflows/build-ranked-mod.yml`
- `.github/workflows/build-ranked-debug.yml`
- `.github/workflows/ranked-ci.yml`
- `.github/workflows/deploy-ranked-lightsail.yml`
- `.github/workflows/publish-ranked-download.yml`
- `ranked/tools/verify-published-clients.py`
- `ranked/tools/verify-production-release.py`
- 관련 R2 배포 및 Geode 패키지 스크립트

현재 관측값(UTC 기준; 최신 변경 후 재확인한다):
- beta.121 Validate: 2026-10-30 16:53:06 ~ 16:58:31, 약 5m25s
- beta.121 Release: 2026-10-30 16:53:06 ~ 17:01:21, 약 8m15s
- beta.121 Debug: 2026-10-30 16:53:06 ~ 17:07:51, 약 14m45s
- beta.121 production deploy: 2026-10-30 16:53:06 ~ 17:08:45, 약 15m39s
- beta.120 deploy: 약 14m11s
- 관련 GitHub Actions run ID: 41093892859, 41093893195, 41093892958, 41093893227, 41092839172

관측값은 workflow 총 경과 시간이지 각 단계의 실행 시간이라고 단정하지 않는다. Actions Jobs API를 다시 조회하여 큐 대기, 네이티브 테스트, 플랫폼 컴파일, 합본 패키징, R2 게시, 서버 Docker 빌드, 패키지 게시 대기를 단계별로 계측한다. 개선 전/후 시간을 표로 남긴다.

### A1. Debug 빌드의 불필요한 직렬 의존성 제거

현재 `build-ranked-debug.yml`의 `build`, `ios-debug-off`, `additional-debug-off`가 `tests` 선행 완료를 요구하여 네이티브 테스트가 끝난 뒤 플랫폼 빌드가 시작된다.

서로 독립적인 `tests`와 플랫폼 빌드/Debug OFF 검증을 병렬 실행하도록 DAG를 재배치한다.

- 플랫폼 빌드의 필수 선행을 `metadata`와 그 결과를 사용하는 최소 작업으로 줄인다.
- `tests`는 빌드와 병렬 실행한다.
- 최종 게시, 운영 반영, 릴리스 성공 판정은 `tests`, 모든 필수 플랫폼 빌드 및 Debug OFF 검증, 패키지 검사 성공을 여전히 요구한다.
- `needs` 변경으로 기존 `if`/`skipped` 조건이나 `workflow_call`의 출력 참조가 깨지지 않도록 확인한다.
- 빌드가 빠르게 끝나더라도 필수 테스트 실패 시 R2와 서버에 게시할 수 없어야 한다.
- iOS/Android/Windows/macOS 전체 변형과 Debug ON/OFF 검증을 운영 릴리스에서 누락하지 않는다.

### A2. Geode 네이티브 빌드의 재사용성 개선

`geode-sdk/build-geode-mod@main`, 고정 bindings ref, SDK, Codegen, CMake, Ninja, toolchain, Gradle/Android NDK 사용 방식을 확인한다.

- 각 OS runner의 실제 캐시 가능 디렉터리와 복원/업로드 시간을 측정한다.
- 공식 액션의 캐시 기능과 안전한 `actions/cache` 키를 먼저 검토한다.
- SDK, Geode bindings, 컴파일 아티팩트, Codegen 등 독립 결과만 입력 해시와 OS/arch/target/config별로 구분해 캐시한다.
- ABI, Geode SDK, C++ 빌드 플래그, Debug ON/OFF, bindings revision이 다르면 잘못된 캐시를 공유하지 않는다.
- 캐시 적중과 미적중의 소요 시간을 비교한다.
- 캐시를 적용할 수 없거나 더 느리면 그 이유를 보고한다. 캐시 성공을 추측하지 않는다.
- `geode-sdk/build-geode-mod@main` 등 움직이는 액션 참조는 공급망 및 재현성 정책을 검토하고 안전하게 pin한다. 무검증 일괄 업그레이드를 금지한다.

### A3. 변경 범위별 빠른 피드백과 전체 릴리스 검증 분리

PR/일반 브랜치 개발에는 빠른 lint/typecheck/관련 단위 테스트 및 변경된 플랫폼 중심의 적절한 검증 경로를 제공한다.

- `main`의 운영 릴리스와 R2 게시에는 전체 필수 회귀/호환/플랫폼 검증을 유지한다.
- Docs-only, 순수 서버 변경, 순수 클라이언트 변경, 워크플로 변경, 공통 버전 변경을 path filters와 의존성 기준으로 구분한다.
- `ranked/apps/server/src/session/client-version.ts`, 공통 규칙, 모드의 `mod.json`, localization, 빌드/패키지 스크립트처럼 실제 cross-dependency가 있는 변경은 절대 잘못 건너뛰지 않는다.
- 장시간 시뮬레이션/가상 soak 및 무거운 PostgreSQL HTTP 통합 테스트는 어떤 변경에 필수인지 검토한다. 단순 제거를 금지한다. 리스크가 큰 규칙/서버/DB 변경이나 운영 릴리스에는 필수 경로를 유지한다.
- 변경 없는 대규모 테스트를 빠른 PR 피드백에서 선택적으로 분리할 경우 full required gate가 반드시 별도 실행되게 설계한다.
- `workflow_dispatch`로 full validation을 실행할 수 있게 한다.
- `workflow_run`, concurrency, branch protection/required checks로 인해 검증이 조용히 건너뛰는 구성을 금지한다.

### A4. 운영 서버 배포의 40분 폴링과 구버전 대기 개선

현재 `deploy-ranked-lightsail.yml`의 `verify-published-clients.py --wait-seconds 2400`은 Release/Debug R2 게시를 최장 40분까지 기다릴 수 있다.

- 실제 Release 및 Debug 두 채널의 immutable version, 파일 해시, manifest/descriptor가 검증된 뒤에만 Lightsail을 활성화한다.
- 가능한 경우 빌드/검증/게시 완료를 명시적 workflow dependency 또는 검증된 완료 신호로 연결하여 불필요한 반복 폴링을 줄인다.
- 운영 배포가 서로 다른 SHA로 생성된 Release/Debug 및 서버를 혼용하지 않게 검증한다. 동일 version 문자열만으로 일치했다고 간주하지 않는다.
- 오래된 실행이 최신 버전을 뒤늦게 덮어쓰지 않도록 stale SHA를 활성화 직전에 검증하고, 이미 superseded된 배포를 안전하게 skip하는 조건을 설계한다.
- 이미 운영 서버를 변경 중인 배포를 `cancel-in-progress: true`로 무조건 중단하지 않는다. 롤백 및 서비스 복구가 먼저다.
- 운영 SSH 키, secret, 프로덕션 환경 보호, immutable Docker 이미지/클라이언트 해시 검증, health/ready, rollback은 유지한다.
- 싱가포르 Lightsail 운영 대상 `52.74.37.130`을 임의로 서울 리전으로 바꾸지 않는다.
- 서버 변경 없는 클라이언트 버전 업데이트에도 required client version 정합성을 검증한다.
- 기존 `[publish-r2]`, `[deploy-lightsail]`, `[build only]`, `[no-deploy]` 의미 및 수동 배포 동작을 파악하여 비호환 변경을 막는다.
- 배포 이벤트가 여러 번 발생해도 단일 커밋에 대해 중복 게시 또는 중복 운영 활성화를 방지한다.

### A5. CI 검증 및 실측

변경된 YAML을 정적 검사하고 actionlint 또는 동등한 검증을 실행한다. 재사용 워크플로 `workflow_call`, `needs`, matrix, permissions, secrets, outputs가 올바른지 검증한다.

- Tests 실패 -> 게시/운영 활성화 차단
- iOS Debug OFF 실패 -> 운영 게시 차단
- R2의 한 채널 누락 -> 서버 활성화 차단
- 구 SHA의 늦은 완료 -> 최신 운영 버전 되돌림 금지
- 성공 시 두 채널 해시 및 공개 운영 health/required version 확인
- 서버의 자동 롤백 유지

변경 전/후 결과를 기록하고 검증되지 않은 3~5분 목표를 실제 달성 시간이라고 보고하지 않는다.

## B. Entropie 특정 구간 RECAPTURE REQUIRED

직접 로그:
- `logs/primary/Geode 2026-10-31 02.03.18.log`
- `logs/primary/Geode 2026-10-30 00.15.34.log`
- `logs/primary/Geode 2026-10-28 22.03.07(1).log`

Level: Entropie, ID 143308401.

확정된 관전자 거부 증거:
1. 2026-10-30 00:31:58: sequence 1435->1436, publisher ms 71279->71295, `(25885.127,4044)` -> `(25894.729,5439)`, distance 1395.0331, SHIP->SHIP, gravity false->false, `type=NONE`, `nativeEvents=0`.
2. 2026-10-31 02:06:30: **같은 Attempt ID, 같은 sequence와 같은 좌표로 재현**. 동일 원본 스트림의 반복 재생일 수 있으므로 독립적인 원본 캡처 실패 두 건이라고 중복 집계하지 않는다.
3. 2026-10-31 02:09:47: **별도의 Attempt**에서 sequence 1438->1439, publisher ms 71245->71295, `(25866.227,3969)` -> `(25892.324,5437.5)`, distance 1468.7318, SHIP, `type=NONE`, `nativeEvents=0`.
4. 2026-10-28 22:07:25: 같은 맵 인근에서 SHIP 불연속 이동 distance 1455.0317, `type=NONE`.
5. 2026-10-28 22:04:03: 별도 SPIDER_SNAP, nativeEvents=0 사건도 존재한다. 원인을 동일하게 추정하지 않고 정책/캡처 직렬화 회귀 테스트로 활용한다.

**증명된 사실**: 특정 맵 지점에서 큰 좌표 변화에 검증 가능한 native snap 증거가 없어서 `SpectatorLiveRuntime`가 `POSE_DISCONTINUITY`로 차단한다. 반복 재생으로 같은 기록이 같은 패킷에서 실패한다.

**미확정 가설**: 실제 게임의 특수 텔레포트/트리거/오브젝트 이동이 캡처되지 않았거나, 캡처된 이벤트가 프레임/샘플 병합, 직렬화, relay, viewer decode 중 사라졌거나, 원본 pose 취득 순서가 섞였다. 이를 반드시 분리 검증한다.

### B1. Native 원인 역추적

중점 파일:
- `corum-ranked-mod/src/PlayLayer.cpp`
- `corum-ranked-mod/src/domain/SpectatorCapturePolicy.hpp`
- `corum-ranked-mod/src/domain/SpectatorRemotePolicy.hpp`
- `corum-ranked-mod/src/spectator/SpectatorPlayerCapture.cpp`
- `corum-ranked-mod/src/spectator/SpectatorNativeInstrumentation.cpp`
- `corum-ranked-mod/src/spectator/SpectatorNativeWorldEvents.cpp`
- `corum-ranked-mod/src/SpectatorLiveRuntime.cpp`
- `corum-ranked-mod/src/spectator/SpectatorWorldEventsCodec.hpp`
- `corum-ranked-mod/src/spectator/SpectatorWorldDriver.cpp`
- `ranked/apps/server/src/survival/survival-spectator-live.service.ts`
- 기존 관련 네이티브/정책/서버 테스트

기존 캡처 경로 `teleportPlayer()`, `PlayerObject::update()`의 `m_wasTeleported` false->true 전환, `spiderTestJump()`를 추적한다. 단순 `PlayerObject::setPosition()`만으로 텔레포트 증거를 만들어서는 안 된다.

원본 플레이어가 X 25,800~26,000, level time 70~73초에 도달할 때에 한해 제한된 계측을 추가한다. 로그 폭주 및 성능 저하를 방지한다.

필수 계측:
- native frame/tick, publisher sample, callback execution order
- player 1/2, mode, gravity, position before/after, velocity, lastPosition
- Teleport portal object/trigger ID 및 호출 함수(증명 가능할 경우)
- `m_wasTeleported` before/after, 이미 true일 때 연속 이동 이벤트 유실 여부
- 이동 발생 지점이 네이티브 물리 업데이트 내부인지, 카메라/렌더/외부 모드의 간섭인지
- `SpectatorCapturePolicy::nativeSnap` 진입/거절 사유
- `m_snap` 연속 이벤트 병합, 최대 8개 초과, source/destination 인접성 검증
- `snapshot()`/`sampled()`의 호출 시점
- publisher sequence, encode/decode, relay, viewer의 event count / event type

개별 단계별 증거를 같은 event ID, tick, epoch, attempt, sequence로 상관관계 분석 가능하게 만든다.

현존하는 네이티브 이벤트가 없으면 반드시 **원본의 실제 이동 함수**를 찾는다. `SHIP`이고 Y가 많이 올랐다는 이유만으로 신뢰 이벤트를 발급하지 않는다.

### B2. 캡처 이벤트 무결성 및 프로토콜 수정

실제 네이티브 이동임이 증명된 경우에만 명확한 원인 타입/증거를 추가한다. 기존 TELEPORT/SPIDER_SNAP에 맞지 않는 실제 네이티브 이동이면 제한된 새 이벤트 타입을 정의하고 기록/전송/검증 코드를 함께 변경한다.

- `before/after`, source callback, event tick, mode/gravity, P1/P2, 순서 보존
- 중간 샘플링이 없더라도 native event chain이 전송될 때까지 보존
- queue/backpressure/retry 시 이벤트의 유실 또는 중복 방지
- 하나의 50ms 패킷 내 여러 native frame 이벤트의 순서 및 chain 검증
- `finiteSpectatorSnap` 및 `verifiedSpectatorSnap`의 시작/끝 좌표 일치
- 타임스탬프/sequence가 서로 다른 Attempt 또는 재진입에 섞이지 않음
- 프로토콜 호환성 및 payload 상한, CPU/메모리 예산 유지
- 디코딩 실패를 조용히 `NONE`으로 만들지 않고 이유 로깅
- 필요한 서버 변경은 additive/backward compatible하게 수행

출처 없는 큰 좌표 변화에 이벤트를 합성하거나 거부 임계값 400을 일괄 증설하는 것은 금지한다.

### B3. 관전자 재생/월드 연결 수정

검증된 불연속 이동 경계에서:
1. 이전 구간과 다음 구간을 별개의 presentation segment로 분리한다.
2. 순간이동 구간을 직선 보간하지 않는다.
3. 승인된 목적지에서 실제 Remote Player 위치와 카메라, Follow Player, Trigger/World 상태를 이어준다.
4. P2 개별 snap 시 P1 트리거 이력을 임의로 초기화하지 않는다.
5. 정상적인 새 Attempt/epoch가 시작되면 차단 상태를 해제한다.
6. 검증 불가 좌표 점프는 `POSE_DISCONTINUITY` 차단을 유지한다.
7. `RECAPTURE REQUIRED` 무한 루프, 중복 재연결, 0~2% 반복, 월드 리셋 및 음악 재시작을 만들지 않는다.

### B4. 회귀 테스트 및 실기기 확인

먼저 첨부 로그의 1435->1436 및 1438->1439 패킷을 이용해 부정 테스트를 추가한다. **증거 없이 큰 이동이 일어나면 반드시 거절**되어야 한다.

그다음 원본 게임에서 실제 생성된 신뢰 가능한 이벤트로 같은 이동을 다시 캡처한 fixture를 만들고, 그 이벤트가 전달되었을 때만 관전이 성공하는 긍정 테스트를 추가한다. 가짜 이벤트를 생성하여 테스트를 통과시킨 결과를 실제 버그 해결로 주장하지 않는다.

필수 시나리오:
- Entropie X 약 25,890 / 71.295초, 여러 독립 Attempt
- 연속 Teleport, Spider, Ship, Dual/P2, mode/gravity 유지 이동
- 여러 이벤트 연쇄, 누락 패킷, 50ms 샘플링, 지연/재시도
- Follow X/Y, Random Trigger, 배경/카메라, 음악, Seek/중도 입장
- 일반 이동 및 출처 없는 큰 좌표 점프 거부 유지
- 관련 서버 relay와 world-driver 회귀
- iOS Debug/Release 빌드 및 실기기 테스트 수행 여부 명시

맵 원본을 재생하거나 단말에서 재현할 수 없는 환경이면 정확한 native 함수는 미확정으로 남기고 추적 가능한 계측을 배포한다. 로그에 없는 원인을 확정했다고 주장하지 않는다.

## C. 추가 확정 실패: beta.121 CBF Replay 충돌 상태 DESYNC 및 재시작 초기화

**이번 패키지의 새 최우선 회귀 수정 항목이다.** `logs/primary/Geode 2026-10-31 02.16.39.log` 및 `REPLAY_DESYNC_CASES.json`을 먼저 확인한다. 기존 CBF 타임스탬프 문제를 다시 추측하여 고치지 않는다. 기존 A/B 작업과 독립적으로 원인을 검증한다.

### C1. 재현 증거와 미확정 부분

동일 Geode 로그에서 `SurvivalReplayDesync` 7건을 확인한다. 그중 독립 Attempt는 4개다. 5건의 `CBF_ROOT:before`는 `ROTATE` 네이티브 루트 진입 전 `$.before[9]`만 불일치한다. 2건의 `CBF_INITIAL_COLLISION_LOG_MISMATCH`는 동일 Attempt 리플레이를 재시작할 때 발생한다.

| Attempt | 첫 실패 프레임 | 기대 `m_lastCollisionBottom` | 실제 `m_lastCollisionBottom` | 첫 불일치 |
| --- | ---: | ---: | ---: | --- |
| `a3cf0fb9-91ff-4c86-b6c2-3da0fc1335f6` | 256 | 2166 | 2599 | `$.before[9]` (동일 Attempt 두 차례) |
| `6ebb718d-8003-4456-a41e-67c3f69ce21f` | 135 | 109 | 123 | `$.before[9]` |
| `ee3ccf79-4ac5-474c-b2b4-147e7ffd1c91` | 234 | 166 | 170 | `$.before[9]` |
| `9c5a433c-eabd-45ef-b92d-dbb3ffd6e72d` | 52 | 213 | 214 | `$.before[9]` |

직전 상태 배열에서 좌표, 회전, Y 속도, Ship 회전, 입력 홀드 및 접지 여부가 같더라도 위 정수 필드가 달라져 `CBF_ROOT:before`에서 중단된다. 이 관측은 '실제 플레이 물리가 동일하다'는 증명이 아니다. 충돌 오브젝트가 다르다면 이후 물리가 달라질 수 있으므로 확인이 필요하다.

**검증된 사실:** 이 오류는 현재 직접 비교되는 충돌 관련 정수 필드 불일치로 발생한다. 동일 Attempt 재시작 시 충돌 로그 개수 검증이 실패한다.

**미확정 가설:** `m_lastCollisionBottom`이 원본 및 재생 로딩에서 달라질 수 있는 런타임 오브젝트 식별값/UUID인지, 서로 다른 오브젝트를 실제로 충돌 대상으로 선택했는지, 충돌 순서 또는 객체 생성 순서가 달라졌는지 확인되지 않았다. UUID라고 미리 확정하지 않는다. 먼저 Geode/GD 바인딩 정의와 실제 값 생성 및 사용 경로를 확인한다.

### C2. 충돌 상태의 정체와 결정론적 비교 수정

중점 파일:
- `corum-ranked-mod/src/replay/SurvivalReplayCbfNative.cpp`
- `corum-ranked-mod/src/replay/SurvivalReplayCbfTrace.hpp`
- `corum-ranked-mod/src/replay/SurvivalReplayPlayerState.hpp`
- `corum-ranked-mod/src/replay/SurvivalReplayPhysicsFields.hpp`
- `corum-ranked-mod/src/replay/SurvivalReplayPlayback.hpp`
- `corum-ranked-mod/src/replay/SurvivalReplayNative.cpp`
- `corum-ranked-mod/tests/SurvivalReplayCbfNativeTest.mjs` 및 Replay Native 회귀 테스트

`SurvivalReplayCbfNative.cpp::motion()`이 `m_lastCollisionBottom`, `m_lastCollisionTop`, `m_lastCollisionLeft`, `m_lastCollisionRight` 네 값을 기록하고, Root의 `before`/`after`를 정수 그대로 비교한다. `CbfBoundaryCursor::enter` 및 `leave`의 정확한 비교 의미를 조사한다.

다음 작업을 수행한다.
1. 해당 충돌 필드의 자료형, 할당 주체, 값 의미(런타임 ID, 맵 ID, 객체 인덱스, 메모리 핸들 등), 물리 판정에서의 실제 사용처를 코드 및 네이티브 바인딩으로 확인한다.
2. 로딩된 동일 레벨의 `geometry hash`, 오브젝트 저장 순서, 객체 위치/종류 및 유일한 맵 내 식별 정보를 이용해 **동일한 충돌 대상을 나타내는지** 구분한다. 동일 종류의 오브젝트가 여러 개 있는 상황에서 `m_objectID`만으로 동일 객체라고 판단하지 않는다.
3. 원본과 재생이 실제로 **같은 맵 오브젝트**를 가리키지만 런타임 정수만 다른 경우에는 저장 형식 및 검증 단계에서 **정규화된 안정 식별자**를 비교한다. 매핑 근거가 불충분하면 억지로 동일하다고 처리하지 않는다.
4. 충돌 대상이 실제로 다른 경우에는 `CBF_COLLISION_TARGET_MISMATCH` 등 구체적인 오류와 객체 증거를 출력하고 DESYNC 검증을 유지한다.
5. `m_lastCollision*` 비교를 일괄 제거하거나, 정수를 단순히 `-1`로 덮거나, 물리 상태를 강제로 위치 보정하여 통과시키는 것을 금지한다.
6. 다른 관련 분야(`m_lastGroundObject`, slope, collided object, collision logs, native object references`)의 객체 신원/생성 순서가 어떻게 비교되는지 검토하고 중복되는 취약한 ID 비교를 수정한다. 맵과 물리 데이터의 신뢰성 검증은 유지한다.
7. 버전별 Replay 파일의 역호환 및 schema upgrade를 검토한다. 이전 정수 ID가 동일성을 보증하지 못한다면 재생 가능을 거짓으로 주장하지 않고, 독립 재캡처 없이 가능한 검증 범위와 불가 사유를 표시한다.

### C3. 재시작과 Seek의 충돌 로그 상태 복원

`validateCbfInitial()`은 `captureCbfInitial()`에서 얻은 네 방향 충돌 로그 개수를 초기 저장값과 비교한다. 현재 `resetLevel()`, `resetReplayInitial()`, `restoreCbfInitial()`, `acquireCbfReplayHooks()`, `releaseCbfReplayHooks()`의 실제 실행 순서가 재시작·Seek·플레이어 재진입에서 달라질 수 있는지 추적한다.

- 첫 진입과 재시작 때의 collision log count, collision flags, lastCollision 정수, lastPosition, P1/P2 모드를 각각 기록한다.
- 정상적인 reset/restore 후에도 이전 재생의 stale collision logs가 남는다면 **원본 네이티브 초기화 순서와 일치하는 범위에서** 안전하게 정리한다.
- 임의 초기화로 실제 충돌 정보를 제거하지 않는다. 소유권, null 포인터 및 네이티브 벡터 생명주기를 검증한다.
- 재시작마다 `CBF_INITIAL_COLLISION_LOG_MISMATCH`가 사라져야 하며, 동일 Replay를 연속 3회 새로 시작한 결과가 같아야 한다.
- Seek, 일시정지, 배속, scene 종료/재진입, CBF hooks lease/restore, 초기화 실패 시 격리 상태를 검증한다.

### C4. 고정된 재현 테스트와 성공 기준

새 회귀 테스트는 아래 구분을 명시한다.
- **정수값만 다르고 실제 오브젝트는 같음:** 안정적인 ID로 비교하면 정상 재생된다.
- **정수값과 실제 오브젝트가 모두 다름:** DESYNC를 여전히 감지한다.
- **서로 다른 오브젝트가 같은 종류 ID를 공유함:** 잘못 동일하다고 판단하지 않는다.
- **객체가 삭제/재생성 또는 순서 변경됨:** 신뢰할 수 없는 매핑을 거부한다.
- **동일 Replay 3회 재시작:** collision logs와 CBF frame/clock 경계가 모두 재현된다.
- **CBF ON/OFF:** Cube, Ship, Robot, Dual/P2, slope, platform, orb, portal 및 충돌 반복 구간을 통과한다.
- **후속 프레임:** 실패 프레임 52/135/234/256을 통과했다는 사실만으로 성공 판정하지 않는다. 완전한 Attempt 종료까지 입력, physics, native event 및 최종 Pose가 맞아야 한다.

컴파일만 통과한 fixture와 iOS/Android/Windows/macOS **실기기 성공**을 명확히 구분한다. 실제 로그의 관련 바이너리 버전 및 CBF 1.5.0 조합을 검증하지 않았으면 성공을 주장하지 않는다.

기존 A 항목의 CI/CD 최적화는 이러한 새로운 회귀 테스트를 건너뛰는 fast path를 만들어서는 안 된다. `main`/R2/운영 게시에 필요한 전체 safety gate는 유지한다.

## D. 기존 Replay 기능 회귀 금지

다음 기존 기능은 유지한다.
- CBF ON/OFF 입력 타임라인과 물리 타임스탬프 동기화
- Cube/Robot/Ship replay DESYNC 검증
- 타임오버/라운드 전환/일시정지 메뉴 복귀의 Attempt terminal 확정
- 원본의 `ATTEMPT_END`, 서버 receipt, 임시 파일 재시작 복구
- 공식 최고기록만 최종 노출, 증거가 없는 부분 재생을 완료로 위조하지 않음
- 계정, LP/MMR/AP, 도전과제, 기록 및 안티치트 정책 불변

`logs/context/replay` 파일들은 회귀 확인에 사용하고 이번 `RECAPTURE REQUIRED`와 물리 DESYNC를 동일한 원인이라고 단정하지 않는다.

## E. 작업 완료 절차

1. 세 작업의 실패 원인과 워크플로 병목을 각각 증명한다.
2. A/B/C 수정에 필요한 최소 범위부터 작업하고 적절한 회귀 테스트를 추가한다.
3. workflow DAG를 정적 검증하고 CI 실패 가드를 실증한다.
4. 네이티브/서버 테스트, 플랫폼 패키지 검사, Replay 충돌 식별/재시작 회귀를 실행한다.
5. 완료된 코드의 최신 main 반영, 정상 통과한 Release/Debug R2 게시와 싱가포르 운영 서버 배포를 기존 절차에 따라 수행한다. 필요한 경우 베타 버전을 현행 최신에서 증가시킨다.
6. required version, R2 파일 해시, Lightsail public health/ready를 실제 확인한다.
7. 실패하거나 환경 권한 부족으로 수행하지 못한 빌드/실기기/운영 작업을 성공으로 보고하지 않는다.
8. 사용자 데이터/기존 베스트/서버 DB 초기화, 강제 push, 현재 검증된 운영 버전의 무조건 중단은 금지한다.

최종 보고서에 다음을 포함한다.
- 원인 확정/가설 구분
- 변경된 파일, 커밋 SHA, 릴리스 버전
- CI DAG 이전/이후, 단계별 분 단위 소요시간, 캐시 적중 여부
- 필수 테스트/플랫폼별 결과, 필요한데 실패한 검증
- Entropie 이동의 출처 함수/증거와 캡처-릴레이-검증 경로
- `m_lastCollisionBottom` 값의 정확한 의미, 안정 ID 매핑 근거, 잘못된 충돌과 단순 ID 불일치 판별 결과
- 같은 Replay 3회 재시작에서 collision log 복원 및 DESYNC 결과
- 71.295초 구간 독립 Attempt 실기기 재현 여부
- R2 Release/Debug 및 Lightsail 배포/롤백 검증 결과
- 남은 리스크와 후속 수정 항목

핵심 성공 조건은 **빠른 빌드에서도 운영 안전성을 유지하고, 검증된 실제 불연속 이동만 정확히 관전하며, CBF 충돌 대상 신원과 초기 상태를 결정론적으로 재현해 끝까지 DESYNC 없이 재생되는 것**이다.
