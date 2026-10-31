# Corum Ranked beta.122 검증 및 배포 보고

2026-10-31 UTC. 마지막 공개 API 직접 확인: 07:02:46 UTC.

코드 반영·자동 검사·두 채널 게시·Singapore 운영 활성화를 완료했다. 실제 GD/CBF 실행은 수행하지 못했다. 특히 Entropie의 정확한 Native 이동 함수와 정상 이동을 입증하는 실기기 양성 fixture는 미확정이다. 이번 Entropie 변경은 사용자 ZIP의 B4에서 허용한 증거 수집 릴리스다. 전체 Native 문제의 실기기 해결 완료로 해석하면 안 된다.

## 버전, 소스 및 실제 배포

- 시작 main: `5875c91cc3bf64b3eb1cfc3c019dcc5ef4f16b68` (beta.121).
- 새 버전: `v0.5.0-beta.122`.
- 최종 main/운영/Release/Debug 공통 SHA: `a122cddce51fdf9c8d8a90c6f81d91b048dd8868`.
- Git tree: `6b2c39dd74bb076d3b8a1cd302f8d9f402fc1b4e`.
- [main 커밋](https://github.com/ybaf100/Corum-Ranked/commit/a122cddce51fdf9c8d8a90c6f81d91b048dd8868).
- [통합 Actions 실행](https://github.com/ybaf100/Corum-Ranked/actions/runs/41177728737), attempt 2 최종 success. 26 jobs 성공, 중복·선택 경로 9 jobs 의도적 skip, 실패 0. 필요한 테스트나 플랫폼을 생략한 것이 아니다.
- attempt 1은 runner step 실행 전 실패했다. 사용자가 워크플로 정상화를 알린 뒤 failed jobs를 재실행했다. 정확한 계정/runner 장애 원인은 확인하지 못했으며 결제 문제라고 단정하지 않는다.
- 기존 정상 커밋 위 정상 fast-forward로 main을 갱신했다. reset/force push 없이 기존 변경을 보존했다. 마지막 확인 HEAD=origin/main=원격 main이며 작업 트리는 clean이다.

| 공개 채널 | 실제 크기 | SHA-256 |
|---|---:|---|
| [Release](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.122/Corum-Ranked-v0.5.0-beta.122-release.geode) | 15,121,293 bytes | `4ec912cdacc902cdae55ded4e4f7ff8e642d16daae81dd7eabadcd90f4b53ec6` |
| [Debug](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.122/Corum-Ranked-v0.5.0-beta.122-debug.geode) | 16,128,301 bytes | `8e952a1c5b78ce610e92ca63e0955d1b8bb118a6286a987bb405ff4eda107ae0` |

두 실제 공개 파일을 다운로드했다. descriptor/내장 mod.json/채널 stamp/sourceSha/크기/해시, Windows x64 PE, Android32/64 ELF, iOS arm64, macOS x86_64+arm64 universal, Native detour/Debug 컴파일 가드, ZIP CRC/중복 항목, 각 채널 18개 SD/HD/UHD 폰트 및 외부 이미지 분리를 검증했다.

Singapore 활성화 job은 06:57:15 UTC에 `Deployment successful: a122...`를 기록했다. 기존 pinned SSH와 host activation entry point를 사용했다. `/health` 200 및 정확한 sourceSha, `/ready` 200 및 DB/schema/config ready, requiredClientVersion beta.122, 양 채널 업데이트 URL/해시/크기 일치를 Actions와 로컬 읽기 전용 검사 양쪽에서 확인했다. Solo/Survival 활성 및 64인 설정이 유지된다. 익명 history/badges/achievements/Survival history/Debug active/spectator targets는 모두 401이다. 인증된 운영 사용자 세션을 임의 발급하지 않았으므로 운영 인증 성공 응답 검증을 했다고 주장하지 않는다. 인증된 기능은 격리 DB 통합 테스트로 확인했다.

## A. 워크플로 원인과 변경

기존 Debug 플랫폼 빌드와 Debug OFF 빌드가 Native tests 완료를 기다렸고, 별도 서버 배포는 패키지 공개를 polling했다. beta.121에서 Native job 완료 16:58:43 이후 macOS Debug가 16:58:48 시작한 것을 실제 API로 확인했다.

새 `ranked-release.yml`은 서버/rules/PG/soak 검사, Native 검사, 양 채널 5개 플랫폼 빌드, Debug OFF 5개 플랫폼 빌드, 서버 이미지 준비를 병렬화한다. 두 채널 게시 모두 전체 검증과 양쪽 빌드 완료를 요구한다. 운영 활성화는 두 채널의 실제 게시와 같은 SHA 이미지가 준비된 뒤 공개 파일을 다시 검사한다. 게시 전 실패, 누락 채널, SHA 혼합, 오래된 main은 차단된다.

단일 parent run에서 upstream의 고정 `geode-build-*` 아티팩트명이 충돌할 수 있어 채널·플랫폼별 이름으로 분리했다. 합본 도구는 정확한 5개 입력만 허용하고 기존 공식 `geode package merge --optimize-pngs`를 사용한다. 최종 합본 역시 플랫폼/리소스를 검사한다. 실제 Release/Debug 합본 및 게시가 통과했다.

production lock은 활성화와 공개 검증에만 적용하고 취소하지 않는다. 이미지 업로드 후 SSH 활성화 직전 main을 다시 확인한다. 이미 같은 SHA가 활성화됐으면 불필요한 재시작 없이 검증한다. 기존 rollback 경로와 pinned host 검증을 유지했다. 운영 실패를 일부러 만들어 rollback을 발동하지는 않았다. 관련 실패 가드 및 정책 테스트는 통과했다.

기존 Geode builder revision, bindings revision, SDK 5.8.2, CLI 3.9.0, source-built Codegen 설정을 유지했다. upstream pinned builder 내부의 CLI setup helper는 여전히 main 참조이므로 모든 전이 의존성을 완전히 고정했다고 주장하지 않는다. upstream checkout@v4의 Node20 강제 Node24 실행 경고가 있었으나 실행은 성공했다.

### 실제 시간 비교

| 단계 | beta.121 기준 | beta.122 정상 attempt 2 |
|---|---:|---:|
| 서버 전체 validation job | 5분 25초 run | 5분 15초 job |
| Native Node 테스트 본체 | 약 4분 59초 | 5분 08초 |
| macOS Release action | 6분 42초 | 3분 56초 |
| macOS Debug ON action | 7분 36초 | 5분 44초 |
| macOS Debug OFF action | 6분 02초 | 5분 49초 |
| Release 패키지 job | 51초 | 49초 |
| Debug 패키지 job | 52초 | 48초 |
| 양 채널 R2 게시 | 별도 채널 runs | 병렬 11초/12초 |
| 새 서버 이미지 준비 | 배포 run 내부 | 35초, 빌드와 병렬 |
| 최종 활성화+검증 job | 별도 run 내 대기 포함 | 68초 |
| 전체 배포 완료 | 15분 39초 | 9분 20초 |

기준 runs: Validate41093892859, Release41093893195, Debug41093892958, Lightsail41093893227. 정상 attempt 2는 06:48:03–06:57:23 UTC이다. 원래 attempt 1 생성 06:42:57부터 최종 성공까지의 사용자 경과 시간은 14분 26초이며, 중간 외부 실패와 사용자 정상화 후 재실행 대기가 포함된다. 정상 실행만 비교하면 약 40.4% 단축이다. 3–5분 전체 릴리스를 달성했다고 주장하지 않는다. 비교는 서로 다른 실제 hosted runner 실행이므로 컴파일 자체의 개선을 단일 원인으로 확정하지 않는다.

macOS Debug ON과 Native job은 각각 06:48:14, 06:48:15에 시작해 직렬 대기가 제거됐음을 확인했다. iOS Debug ON은 Mac runner 수용량 때문에 06:50:16에 시작했다. 이는 테스트 dependency 대기가 아니다.

| 플랫폼 | Release job | Debug ON job | Debug OFF job | 결과 |
|---|---:|---:|---:|---|
| Windows | 5분 48초 | 5분 53초 | 4분 28초 | 전부 success |
| macOS | 4분 08초 | 5분 58초 | 6분 04초 | 전부 success |
| iOS | 2분 56초 | 4분 49초 | 2분 00초 | 전부 success |
| Android32 | 4분 02초 | 4분 05초 | 3분 56초 | 전부 success |
| Android64 | 3분 56초 | 3분 55초 | 2분 46초 | 전부 success |

### 실제 캐시 측정과 한계

- Windows Release: compiler archive 약696MiB 복원 약7.0초, sccache 105 requests 중17 hits/88 misses, 적중률16.19%, errors0. 압축 및 업로드 약5.5초, 업로드803,890,097 bytes.
- Android64 Release: archive 약913MiB 복원 약9.5초, 17 hits/88 misses,16.19%, errors0. 압축 및 업로드 약5.3초,1,059,592,637 bytes.
- 두 사례 모두 CPM 약27MiB source cache가 별도로 적중했다. compiler archive 키는 upstream OS/target 형식을 유지하되 개별 컴파일 내용 키의 custom buster에 SDK/bindings/OS/target/config/Debug ON/OFF/ABI 관련 입력을 넣었다. 전체 source SHA를 키로 삼지 않는다.
- macOS ON source cache 복원 약0.7초, OFF 약2.5초. compiler cache는 사용하지 않는다. upstream multiarch 경로가 검증되지 않은 custom nightly sccache를 내려받는 점 때문에 이를 활성화하지 않았다.
- 같은 commit의 완전 cold/warm runner 두 실행을 추가로 수행하지 않았다. 캐시가 있다는 사실을 고적중·컴파일 단축의 증거로 해석하지 않는다. 이번 릴리스의 첫 namespace 적용과 hosted runner 편차를 포함한 관찰값이다.

## B. Entropie: 확정 증거와 미확정 원인

같은 Attempt의 seq1435→1436, Native71279→71295ms에서 Ship Y4044→5439(1395)가 관찰된다. 이 사례가 다른 로그에도 반복되지만 별도 원본 사건 두 개로 세지 않았다. 별도 Attempt의 seq1438→1439,71245→71295ms에서는 Y3969→5437.5(1468.7)가 관찰된다. viewer packet에 이를 입증하는 Native event가 없다는 사실은 확정이다. 정확히 어느 Native 함수가 이동시켰는지, flag edge/callback/capture/전송 어느 단계에서 증거가 사라졌는지는 미확정이다.

`SpectatorSourceTrace`는 level143308401의 Native70–73초/X25800–26000 범위에서 world/player update, collision, portal, setPosition을 관측한다. Attempt당 일반160/중요32개 및 일반 경로50ms 간격으로 제한한다. before/after, velocity/mode/gravity/lastPosition/latched teleport flag, object IDs, Native step, world frame, callback order와 Source Attempt/recording identity를 기록한다. setPosition 관측을 유효한 Teleport 증거로 자동 승격하지 않는다.

Capture에서 CAPTURED/INVALID_BODY/SPIDER_MODE_OR_GRAVITY/CHAIN_GAP/INVALID_EVENT_OR_CHAIN_LIMIT 이유를 구분한다. 적법하게 캡처된 event의 diagnostic identity는 Native codec→서버 parser→Debug upload/저장/재조회 및 Survival relay→viewer decode까지 보존된다. Source QUEUED/ENCODED/PUBLISH, 서버 ACCEPTED/STORED/LOADED, viewer RECEIVED/DECODE_REJECT 로그가 같은 context/sequence를 연결한다. 이 진단 정보 자체에는 Pose를 승인하는 권한이 없다.

원본 패킷 2개는 실제 viewer decoder의 음성 fixture로 추가했다. Native 증거가 없으면 last valid Pose를 보존하고 recapture 상태로 남으며 실제 network disconnected로 오인하거나 같은 immutable 기록을 계속 리셋하지 않는 것을 검증했다. 기존 400-unit 한도, Native event 연속성/개수 제한, P1/P2 분리, camera/audio/world ownership 검사를 유지했다.

실제71.295초 구간의 독립 Attempt 재현과 정상 이동 양성 fixture는 확보하지 못했다. 다음 실제 GD 캡처로 로그 경로별 최초 유실 지점을 확정해야 한다. Sequence353 Spider와 과거1455-unit 사건을 이번 진단만으로 해결했다고 주장하지 않는다.

## C. CBF Replay 충돌 ID 및 재시작

원본 11개 로그와 ZIP checksum을 확인했다. beta.121 DESYNC는 네 Attempt에서 총7회다. 최초 ROOT ROTATE 실패5회는 frames52/135/234/256의 raw bottom213→214,109→123,166→170,2166→2599 차이를 포함한다. 그 시점의 나머지13개 motion 값 일치는 확인됐으나 실제 충돌 대상이 같다는 증거가 아니다. 나머지2회는 재시작 초기 collision dictionary 불일치다. 저장된 초기 P1/P2 dictionaries는 모두 count0, 초기 scalar4개는 -1이었다.

Bindings의 lastCollision 필드는 int이며 Native 구현 본문은 공개되지 않았다. 따라서 과거 정수가 정확히 UUID인지, 각 값이 같은 오브젝트를 가리켰는지는 미확정이다. raw 숫자가 다르다는 이유만으로 무시하거나 object type만으로 같다고 간주하지 않는다.

새 `SurvivalReplayCollisionIdentity`는 순서가 포함된 map definition SHA-256, map index/objectID, 실제 Native collision 후보의 동일 loaded object/uniqueID와 해당 P1/P2·방향 dictionary key의 동시 관측을 요구한다. 이를 모두 통과한 경우에만 안정 참조로 비교한다. runtime pointer/ID는 manifest에서 제외하고 원래 맵 속성·순서를 검증한다. 중복/모호함, 다른 대상, 삭제·재생성, 순서 변경, actor/방향 증거 혼용은 거부한다. retained references로 pointer 재사용을 방지하고 새 Source Attempt에서 이전 witness를 폐기한다.

ROOT/cursor의 4방향 collision 비교를 유지한다. 같은 대상의 runtime ID만 달라진 경우 안정 참조로 통과하고, 좌표가 같아도 다른 실제 대상이면 `CBF_COLLISION_TARGET_MISMATCH`로 실패한다. raw IDs와 제한된 Native witnesses를 진단에 보존했다. 좌표/회전/속도/입력/clock/RNG 검사는 완화하지 않았다.

재시작은 완료된 owned Native reset 이후, 현재 Scene 소유권·manifest·모든 dictionary 접근 가능·원본 초기 dictionary가 검증된 empty인지 확인하고 그 empty dictionary만 복원한다. 그다음 엄격 검증과 기존 전체 물리 초기 상태 복원을 수행한다. helper가 scalar4개를 -1로 바꿔 실패 프레임을 통과시키는 방식은 사용하지 않았다. before/expected/after dictionaries와 IDs를 기록한다. 원본 nonempty/unknown 초기 내용은 추측하지 않고 거부한다.

실제 production root/cursor와 독립 Native movement double로280 frames의 Attempt 종료까지 P1/P2 Cube/Ship,0.5x/1x/2x/4x,pause/seek,hook lease 실패/복구, 같은 replay3회 완전 재시작을 검증했다. stale collision dictionaries의 owned 복원, 잘못된 대상/없는 witness/old epoch를 거부하는 음성 검사도 통과했다. MACOS/WINDOWS 두 설정에서 통과했다. 이 결과는 실제 GD의 충돌/회전 물리 성공을 의미하지 않는다.

기존 format1/nativeInput2/3 파일은 파싱·저장·Best 보존을 유지한다. 원본 object witness 없는 옛 숫자 ID 기록은 안전하게 변환할 수 없다. 가능한 prefix를 검증하되 증거가 필요한 경계에서 `CBF_LEGACY_COLLISION_IDENTITY_UNAVAILABLE` 및 HUD `COLLISION IDENTITY UNAVAILABLE`로 명시적으로 보류한다. 필수 상태가 없는 리플레이를 완전 호환 재생한다고 주장하지 않으며, 최고기록 삭제·변조나 서버 공식 Attempt/점수 신규 제출은 하지 않는다. MIT CBF 관련 기존 attribution은 유지했고 PolyForm Strict/GPL/라이선스 불명 프로젝트 코드를 직접 복사하지 않았다.

## 자동 검증 결과

| 검증 | 실제 최종 결과 |
|---|---|
| Rules | 361 passed |
| 서버 전체 CI | 710 passed, 기존 선택 pool-reconnect1 skipped |
| Native 전체 CI | 329 passed, fail0/skip0 |
| 독립 C++ domain 프로그램 | 10개 strict build/run passed |
| CI PostgreSQL16 HTTP | 66 passed |
| 별도 로컬 PostgreSQL16 HTTP+pool-reconnect | 67 passed, skip0 |
| 최종 collision identity 대상 재실행 | MACOS/WINDOWS2 passed (전체329와 중복 집계하지 않음) |
| workflow/publication/production guards | 15 passed |
| 채널별5개 패키지 합본 경계 검사 | 4 passed |
| ModReleaseBuild / ModReleasePublish | 2 / 10 passed |
| 추가 asset package 대상 검사 | 3 passed |
| workspace typecheck/build | passed |
| actionlint1.7.7 전체 YAML | passed |
| NORMAL 시뮬레이션 및 가상 장시간 | 1,050경기/10시간 passed |
| GD relay / Apps Script | passed |
| Release/Debug ON/Debug OFF | 각각5개 플랫폼, 전부 passed |
| 실제 공개 패키지/운영 API 검사 | passed |

pool-reconnect의 connection termination은 격리된 로컬 DB에서만 실행했다. 운영 DB에서 실행하지 않았다. 이전 개발 중 실패한 doubles/전환 중 workflow 단언은 수정 후 재실행했으며 최종 CI 실패는 없다. 임의의 mock 통과를 실제 Native 성공으로 취급하지 않았다.

## 데이터 보존

이번 변경에 DB migration은 없다. 기존 운영 Neon 및 ledger를 유지했다. 전후 읽기 전용 확인(06:11:08→07:01:05 UTC):

| 항목 | 전/후 동일 |
|---|---:|
| 비봇 사용자 | 1 |
| Survival matches | 17 |
| 영구 round badges | 1429 |
| 일반 Survival Attempts | 8149 |
| Tiebreak Attempts | 263 |
| Solo Attempts | 0 |
| AP ledger / achievement unlocks | 25 / 25 |

뱃지 행 digest `3b590c968b2314fb4ad24bb276fdd872`, Survival 프로필 전체행 digest `fcafb1d1f00f3175d3a7536e8844073b`, Solo 프로필 전체행 digest `7c19e6ade222177226aa1851c8765835`도 동일하다. 이 측정 범위를 DB 모든 테이블의 byte 단위 감사로 확대 해석하지 않는다. 운영 환경변수·Secrets·계정·기록 초기화는 하지 않았다. 기존 뱃지/HUD/6Attempt/점수/LP/AP/업적 규칙 변경은 이번 패치 범위가 아니다.

## 파일 및 증거 위치

핵심 구현: `.github/workflows/ranked-release.yml`, `ranked-native.yml`, `ranked-feedback.yml`, 기존 build/deploy/CI workflows; `SurvivalReplayCollisionIdentity.hpp`, `SurvivalReplayCbfNative.cpp/.hpp`, `SurvivalReplayCbfTrace.hpp`, `SurvivalReplayNative.cpp`, `SurvivalReplayPlayback.hpp`, `PlayLayer.cpp`; `SpectatorSourceTrace.hpp`, `SpectatorCapturePolicy.hpp`, Native codec/DebugRuntime/liveRuntime 및 서버 Debug/relay/parser; 채널 합본·게시·운영 검증 도구와 관련 tests. 버전 metadata 및 changelog를 포함해62개 파일을 변경했다.

- 저장소 상세 설계: `/workspace/Corum-Ranked/ranked/docs/workflow-recapture-collision-beta122-diagnosis.md`.
- 모든 변경 파일·배포·API·DB 수치: `/workspace/scratch/beta122-completion-report.json`.
- 실제 게시 파일·descriptor·검사: `/workspace/scratch/beta122-public/`.
- 원본 증거 집계: `/workspace/scratch/beta122-replay-raw-evidence.json`.
- 최종 Actions run/jobs: `/workspace/scratch/beta122-actions-final-0.json`, `beta122-actions-final-1.json`.
- CI job/활성화/게시 원본 로그: `/workspace/scratch/beta122-job-*.log`, `beta122-action-job-*.log`.
- 전체 Native/서버/로컬 PG 로그: `beta122-native-all3.log`, `beta122-server-all2.log`, `beta122-postgres-integration.log`.

## 남은 검증

실제 GD2.2081 및 CBF1.5.0에서 네 원본 Attempt, 같은 replay3회 재시작, Robot/경사/플랫폼/Orb/Portal의 실제 dictionary 의미, Cube/Ship/dual/Seek/배속을 다시 실행해야 한다. Entropie71.295초 독립 Attempt의 Source trace에서 이동 함수와 최초 유실 경계를 확인해야 한다. iOS/Android/Windows/macOS 실제 앱 실행·화면·오디오 청취는 이번 환경에서 수행하지 못했다. 이 항목들이 검증되기 전에는 Native DESYNC 및 Entropie 문제가 실기기에서 완전히 해결됐다고 보고할 수 없다.
