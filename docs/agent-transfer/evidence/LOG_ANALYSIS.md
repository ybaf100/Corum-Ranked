# RECAPTURE REQUIRED 및 관련 로그 분석

## 직접 확인된 실패

- `Geode 2026-10-30 00.15.34.log` 2550-2552행: 00:31:58 `sequence 1435 -> 1436`, `publisherMs 71279 -> 71295`, 이전 좌표 `(25885.127, 4044)`, 다음 좌표 `(25894.729, 5439)`, 거리 1395.0331. SHIP, 중력 동일, `type=NONE`, `nativeEvents=0`, `recaptureRequired=true`. Attempt `9f60995d-11b1-41b5-9e90-19002e7b2577`.
- `Geode 2026-10-31 02.03.18.log` 1488-1490행: 02:06:30, 동일 Attempt `9f60995d-...`, `sequence 1436` 및 좌표가 완전히 동일. **이는 동일 원본 기록의 반복 재생이므로 독립적인 원본 이벤트 누락 사례 두 개로 집계해서는 안 됨**.
- `Geode 2026-10-31 02.03.18.log` 2364-2366행: 02:09:47 별도 Attempt `dcfad082-38f1-4ae0-b488-d0d1abf0a63d`, `sequence 1438 -> 1439`, `(25866.227,3969)` -> `(25892.324,5437.5)`, 거리 1468.7318, `type=NONE`, `nativeEvents=0`.
- `Geode 2026-10-28 22.03.07(1).log` 2208-2209행: 이전 유사 SHIP 실패, 거리 1455.0317, event `NONE`.
- 위 10/28 로그 1194-1195행: 별도 SPIDER_SNAP 불일치, distance 119.0221, `nativeEvents=0`; 유형이 달라 별도 추적 필요.

## 코드 확인(당시 main beta.121)

- `SpectatorLiveRuntime.cpp`: `kPoseDiscontinuityDistance=400`, 검증 실패 시 `setFallback("POSE_DISCONTINUITY")`.
- `SpectatorRemotePolicy.hpp`: `finiteSpectatorSnap`, `verifiedSpectatorSnap`이 네이티브 이동 증거의 chain과 전후 좌표를 검증.
- `SpectatorCapturePolicy.hpp`: `nativeSnap`, `secondaryNativeSnap`, `snapshot`, `sampled`가 이벤트를 모아 샘플링하며 최대 8개 이벤트 및 인접성 검사 존재.
- `PlayLayer.cpp`: teleportPlayer, `m_wasTeleported` 이전 false -> 다음 true, spiderTestJump 이벤트 캡처 경로 존재. `setPosition` 자체를 네이티브 순간이동이라고 인정하지 않음.

## 원인에 대한 신뢰도 구분

확정:
1. 특정 맵의 거의 동일한 원본 진행 구간에서 큰 좌표 불연속 이동이 반복된다.
2. 관전자 패킷에는 검증 가능한 native 이벤트가 없는 것으로 decode된다.
3. 400 이동 임계값과 증거 부재 때문에 RecaptureRequired로 차단된다.

미확정:
1. 원본 플레이어에서 실제로 호출된 native function 또는 map trigger.
2. 출처가 없는 위치 변경인지, 진짜 native Teleport 이벤트가 누락되었는지.
3. 이벤트가 처음부터 없었는지, capture/queue/serialize/relay/decode 중 소실됐는지.
4. 외부 모드와의 물리 상호작용 여부.

원본 레벨 143308401 진행 약 71.295초, X 약 25890 영역에 한정해 publisher-side source instrumentation을 추가하는 것이 중요하다.

## 과거 replay logs의 사용 범위

- `Geode 2026-10-29 23.16.41(1).log` 및 `.log`는 상당 부분 중복되므로 동일 DESYNC를 독립 사건으로 중복 집계하지 않는다.
- `Geode 2026-10-30 19.55.15.log`는 이전의 CBF_ACTIVE_TIMING_UNSUPPORTED 증거.
- `Geode 2026-10-30 21.59.54.log`는 이전 Frame 0 NATIVE_PHYSICS_STATE/시간축 불일치 증거.
- `Geode 2026-10-30 07.33.16.log`는 이전 SHIP DESYNC 증거.
- 이번 관전 문제와 기존 Replay DESYNC의 동일 원인 여부는 증명되지 않았다.

## 2026-10-31 02.16.39 추가 Replay DESYNC (beta.121)

- 새 원본 로그: `logs/primary/Geode 2026-10-31 02.16.39.log` (1662행).
- `SurvivalReplayDesync` 7건: `CBF_ROOT:before` 5건, `CBF_INITIAL_COLLISION_LOG_MISMATCH` 2건. 독립 Attempt는 4개.
- `a3cf0fb9-...`: frame 256, `ROTATE`, `$.before[9]`, 기록 2166 / 재생 2599 (2회). 재시작 시 별도 `CBF_INITIAL_COLLISION_LOG_MISMATCH`.
- `6ebb718d-...`: frame 135, `ROTATE`, 기록 109 / 재생 123. 재시작 시 초기 collision log mismatch.
- `ee3ccf79-...`: frame 234, `ROTATE`, 기록 166 / 재생 170.
- `9c5a433c-...`: frame 52, `ROTATE`, 기록 213 / 재생 214.
- CBF `motion()` 배열 인덱스 9는 `m_lastCollisionBottom`이다. 이 정수의 정확한 네이티브 의미(런타임 UUID/객체 식별값/다른 필드)는 코드만으로 확정되지 않았다.
- 다른 물리/입력 필드가 첫 실패 순간 같더라도 충돌 대상의 실제 동등성이 증명된 것은 아니다. 안정 오브젝트 식별 검증 후 true desync와 false positive를 구분한다.
- 재시작 실패는 `validateCbfInitial()`의 collision log count 불일치로 발생한다. 어떤 reset 경로에서 이전 로그가 남는지 추적한다.
- 예전 Frame 0 timestamp DESYNC 및 Entropie RECAPTURE는 별도 유형이므로 동일 근본 원인으로 합치지 않는다.
- 상세 구조화 데이터는 `REPLAY_DESYNC_CASES.json` 참조.
