# Survival M5 — v0.5.0-beta.68

기준 main: `4bf7f7600aae6f504e062474d31f4fdf4cbb0872` (beta67, M3 PR #4 / M4 PR #5 포함).
작업 브랜치: `feat/survival-m5-online`. PR로 제출하며 main 병합·운영 배포는 별도 승인을 기다린다.
M3 영속 reducer와 GD 제공자, M4 PlayLayer·HUD·봇·정산을 확장한다. 별도 서버, Redis, 관전 기능은 추가하지 않는다.

## 확정된 경기 규칙

16명이 모이면 즉시 시작한다. 가장 오래 기다린 참가자의 현재 대기 주기가 180초가 되면 2~15명으로 시작한다.
1명만 있으면 그 참가자의 대기 주기를 다시 180초로 시작한다. 0명인 대기열에서는 경기를 만들지 않는다.
기본 최소 인원은 8명이고, 2~7명은 사용자 승인에 따른 축소 결승 경기다. 티어 제한은 없다.

| 최초 참가 인원 | 라운드별 기본 진출 인원 |
|---|---|
| 2~3 | Final → 1 |
| 4~7 | Round 3 난이도 → 3, Final → 1 |
| 8 | 5 → 3 → 2 → 1 |
| 9 | 6 → 3 → 2 → 1 |
| 10 | 6 → 4 → 2 → 1 |
| 11 | 7 → 4 → 2 → 1 |
| 12~13 | 8 → 5 → 2 → 1 |
| 14 | 9 → 5 → 3 → 1 |
| 15 | 9 → 6 → 3 → 1 |
| 16 | 10 → 6 → 3 → 1 |

8~16명은 최초 인원 N에 대해 `round(N × 10/16)`, `round(N × 6/16)`, `round(N × 3/16)`을 고정한다.
공동 커트라인은 모두 통과하며, 늘어난 실제 진출 인원으로 다음 목표를 다시 계산하지 않는다.
4~7명은 첫 라운드 화면에 ROUND 1을 표시하되 서버 맵 단계는 ROUND_3다. 모든 일반 라운드와 추가 결승은 180초다.
일반 기록은 원래 진행률로 비교하고 화면에서는 정수로 표시한다. 추가 결승만 0.001% 반올림·최고 기록·3 Attempts를 적용한다.
3회를 마친 사람은 기다리며, 전원 완료 또는 180초 종료에 판정한다. 미사용 시도는 0%, 접수된 진행 중 최고 기록은 인정한다.
Final 공동 1위와 추가 결승 동점은 다시 경기한다. 동점 해소를 위한 임의 ID 순위 판정은 하지 않는다.

## LP와 개인 결과

기존 16위 보상표를 유지한다. 최초 인원 N, 실제 점유 마지막 순위 r에 대해
`rewardPosition = 1 + round((r - 1) × 15 / (N - 1))`로 원래 16인 표를 조회한다.
예를 들어 8명 경기의 단독 순위별 보상은 `+50,+30,+20,+10,+2,-20,-40,-55`다.
일반 공동 순위는 점유한 가장 낮은 순위의 LP를 적용한다. 더 늦은 라운드의 탈락자가 앞선 라운드의 탈락자보다 앞선다.

명시적 항복은 현재 라운드에 참가했던 전체 인원 n의 **n위**로 고정하고 즉시 정산한다.
항복자가 추가되면 같은 n위를 표시하되 점유 마지막 순위를 n보다 늘리지 않는다. 먼저 지급한 LP를 다시 계산하지 않는다.
확인창은 최저 등수, 종료, LP 적용 후 로비 복귀를 명시한다. 완료된 라운드의 탈락 연출 중에는 항복 조작을 허용하지 않는다.
일반 탈락은 라운드 종료로 순위가 확정된 후 정산한다. 다른 실제 참가자의 경기는 계속된다.
기존 승급·강등 보호·공통 GOLD와 계정당 한 번 Claim/AP 정책을 그대로 사용하며 Solo LP/MMR을 변경하지 않는다.

정산 키는 `survival:<matchId>:result:<participantId>`이고 M1 `SurvivalService.settleLp`의 트랜잭션·ledger 고유 키를 재사용한다.
개인 결과가 먼저 저장된 뒤 전체 경기가 끝나면, 다른 참가자의 최종 순위만 결과 roster에 보충한다. LP를 다시 지급하지 않는다.
미확정 다른 참가자는 결과 화면·기록에서 0위로 표시하지 않는다. 개인 LP/티어 모션 완료 후 로비로 복귀하며 결과는 기록에서 다시 조회한다.

## 공개 대기열과 봇 충원

공개 경기는 Release/Debug가 같은 대기열을 사용한다. DB에서 FIFO 후보와 살아 있는 인증 세션을 잠근 뒤 한 트랜잭션에서
참가자·LP 스냅샷·경기·온라인 소유자·MATCHED 배정을 확정한다. 계정별 큐 PK와 참가 배정으로 중복 등록을 막는다.
Join 재시도는 대기 시간을 초기화하지 않는다. Leave보다 경기 배정이 먼저 확정되었다면 기존 경기를 반환한다.
Solo Queue, Placement, 독립 M4 Debug와 동시에 참가할 수 없다. 앱 재실행 시 대기열 소유 세션만 교체하고 대기 주기는 보존한다.

Debug 빌드 대기 화면의 봇 추가 버튼은 기존 Debug 비밀번호를 요청한다. 서버가 요청자의 Debug 채널·비밀번호·환경·버전·큐 참가 여부를 확인한다.
같이 기다리는 일반 빌드 참가자에게는 Debug 인증을 요구하지 않는다. 서버가 그 순간의 실제 후보를 잠가 남은 자리를 채운다.
여러 Fill 요청, Join/Leave와의 경합은 같은 admission 잠금과 저장된 요청 UUID로 중복 매치/봇 생성을 방지한다.
봇 수는 클라이언트 입력으로 받지 않는다. Release 패키지에는 봇 충원 UI나 Debug API 경로를 넣지 않는다.

봇 LP는 실제 참가자 평균과 난이도 그룹을 유지하도록 분산한다. 가상 LP는 경기 스냅샷에만 존재한다.
beta67 봇 규칙을 유지한다: 초기/후보 N(50,20²), 정수 0~100%, 20/50/80/110/140초에 각각 40% 확률,
이전 최고보다 낮은 후보 무시. 기존 3 Attempts 추가 결승도 재사용한다. seed와 소비된 이벤트 cursor는 DB에 저장한다.
봇은 사용자 계정, 공식 LP 프로필, 업적을 만들지 않는다.
한 사람이 탈락해도 다른 실제 참가자가 살아 있으면 계속 진행한다. 마지막 실제 참가자가 탈락하면 현재 단계 기록으로 종료하고
이전 탈락 단계·명시적 항복의 고정 순위를 보존한다. 봇 공동 우승은 허용하며 봇 LP는 결과 정보에만 존재한다.
독립 M4 Debug의 1명+15봇/평균 티어/Force n 경로와 정책은 그대로 유지한다. 공개 혼합 경기에는 Force n을 노출하지 않는다.

## 준비·시계·재접속

매칭 및 라운드 사이 roster는 서버 공통 시각으로 5초 동안 유지한다. 한 참가자의 연출 완료 요청이 전체 경기를 막지 않는다.
기존 GD Relay와 `GdSurvivalMapProvider`가 난이도·ID 40,000,000 이상·Rated·비 Auto·비 Platformer·현재 다운로드 데이터를 검증한다.
클라이언트는 서버 certificate와 native geometry SHA-256/크기/버전/난이도를 확인하고 기존 맵·음악·SFX 준비 경로로 자동 입장한다.

연결 중인 참가자의 기존 준비 규칙은 10초 경고/15초 마감, 진입 요청 후 전원 미진입 5초 재선정,
최초 실제 입장 후 10초 책임 판정, 실제 참가자 공통 3초 COUNTDOWN이다. 정상 참가자에게 다른 참가자의 준비 실패를 전가하지 않는다.

접속 종료자는 준비 마감/입장 마감 전에 최근 presence와 관찰한 round/map generation으로 구분한다.
`absent`는 준비 barrier에서만 제외한다. native `assetsReadyAt`/`enteredAt`을 서버가 대신 만들어 주지 않는다.
Absent 참가자도 다음 라운드에 0%로 남고, 돌아오면 certificate → 실제 native 진입 → Attempt 순서로 즉시 기록을 올릴 수 있다.
서버 시계는 정지하지 않는다. 이미 탈락하면 PlayLayer로 보내지 않고 개인 결과를 복구한다. 항복은 되돌릴 수 없다.
**전원이 한 라운드 내내 복귀하지 않아도 0% 판정을 계속한다. LP 없는 자동 취소는 하지 않는다.**
따라서 전원 0% 동점으로 진출하거나 Final/Death Match가 반복될 수 있다. 사용자 최종 결정에 따른 동작이다.

HTTP polling을 유지한다. 큐는 2초 heartbeat, 온라인 경기 기본 poll은 500ms이며 로컬 시계는 서버 시간을 부드럽게 표시할 때만 쓴다.
기존 revision·roundKey·mapGeneration·native 준비 context와 요청 UUID로 오래된 응답/패킷을 거절한다.
새 세션으로 교체되면 이전 요청 task를 취소하고 generation을 바꾼다. 서버도 쓰기 트랜잭션에서 revoked/만료 세션을 다시 확인한다.
이미 수락된 중복 이벤트는 현재 상태를 반환하고, 같은 UUID의 변경된 payload와 종료된 참가자의 새 이벤트는 거절한다.
진행 중 Attempt는 복구 응답의 서버 접수 최고 기록으로 종료한 뒤 새 native Attempt를 시작한다. DM 사용 횟수는 초기화하지 않는다.

## DB / 서버 인터페이스

추가 마이그레이션: `0024_survival_m5_online.sql` (재실행 가능, 기존 테이블/자료 삭제 없음).

| 테이블 | 용도 |
|---|---|
| `sur_mode_state` | 다중 서버 admission/OFF generation fence |
| `sur_queue_entries` | 계정별 큐, 대기 주기, 세션 lease, 경기 배정 |
| `sur_online_matches` | 공개 경기 분류, 봇 seed/cursor |
| `sur_online_players` | 참가 소유자, presence, 복구 세션, 개인 정산 결과/확인 |
| `sur_queue_fill_requests` | 계정별 Fill UUID receipt |
| `sur_request_rates` | 계정별 DB 원자 rate window |

프로세스 재시작은 M3 `sur_matches.snapshot`/projected round/attempt와 위 테이블에서 복구한다.
마이그레이션/필수 테이블이 빠진 서버는 startup schema 확인을 통과하지 못한다.
Survival Admin 변경/초기화도 admission 잠금을 먼저 얻으며, 미정산 온라인 경기를 초기화할 수 없다.
Survival DB 초기화 시 해당 큐·배정·Fill receipt도 정리하고 Solo/AP 자료는 보존한다.

모든 아래 경로는 기존 `SessionGuard`를 사용한다. Join/Fill은 최신 버전과 설치 모드 검사를 추가한다.
기존 GD 로그인/세션 모델을 재사용하며 새로운 GD 계정 소유 증명 프로토콜을 추가하는 작업은 포함하지 않는다.

| HTTP | 경로 (`/api/ranked/survival` 기준) | 내용 |
|---|---|---|
| POST | `/queue/join` | `{installedMods}`; 중복 진입/활성 경기 복구 |
| POST | `/queue/leave` | 큐 이탈 또는 이미 확정된 배정 반환 |
| GET | `/queue/status` | 인원, wait cycle, 서버 시각/남은 시간, 상태/배정 |
| POST | `/queue/heartbeat` | 본인 큐 lease 갱신 + 상태 |
| POST | `/queue/fill-bots` | `{installedMods,password,requestId}`; 서버가 봇 수 확정 |
| GET | `/online/active` | 본인의 미확인 경기/개인 결과와 현재 큐 |
| GET | `/online/matches/:id` | 본인의 경기 상태; 선택 query `roundKey,mapGeneration` presence |
| POST | `/online/matches/:id/events` | 본인 ASSETS_READY/ENTERED/Attempt/진행률/FORFEIT |
| POST | `/online/matches/:id/acknowledge` | 정산 결과 확인, 큐 배정 해제 |

이벤트 형식은 기존 M4 DTO를 재사용한다. 다른 participant ID, 맵 선택, 최종 순위/LP 제출을 받지 않는다.
동일 라운드/맵 certificate, 서버 접수 시각, 본인 Attempt와 0~100 유한 기록만 허용한다.
계정당 분당 큐 60 / Fill 5 / 이벤트 600 / 상태 600 제한이다. 기존 Solo rate 정책은 바꾸지 않는다.
`SurvivalEngineService.createWithin(..., presentationControlled=true, online=true)`는 trusted 매칭 포트이며 공개 생성 HTTP로 노출하지 않는다.
일반 M3/M4 `create`는 여전히 정확히 16명을 요구한다.

## 검증 및 한계

로컬 서버 규칙/전체 회귀, PostgreSQL 16 다중 연결 HTTP 시나리오, C++ domain/native API double, Android64 Debug/Release 컴파일을 수행한다.
HTTP 통합 검사는 실제 localhost Nest 서버·Bearer 세션·SQL·트랜잭션·reducer·ledger를 사용하며,
GD 제공자와 서버 시각만 fixture다. 시간 가속 검사는 180초 실제 플레이·실기기 E2E로 보고하지 않는다.
큐 초과/동시 Fill, 8/12/15/16명, 2/3/4/7명 축소, 1명 재대기, 준비 중 복귀, 전원 0%,
중간 Attempt 복구, 반복 .001% DM, 즉시 공동 최하위 항복, 전 라운드/개인 지급, seed/cursor 복구를 포함한다.
CI는 같은 HTTP suite를 별도의 PostgreSQL 16 서비스에서도 실행한다. 플랫폼 빌드 결과는 PR Actions에서 확인한다.

로컬 검증 결과:

- 공통 규칙 218개, 서버 전체 회귀 466개 (온라인 HTTP 31개 포함).
- 동일 온라인 HTTP 31개를 PostgreSQL 16의 별도 schema / 24개 연결 pool에서도 실행한다.
- 기존 native client 검사 175개와 신규 공개 전송 Release/Debug 검사 2개, 10개 C++ domain 실행 파일.
- Android64 Debug / Release C++ 컴파일, 서버 빌드, 전체 workspace 타입검사.
- 기존 GD Relay / Apps Script / 패키지 게시 정책 검사. 폰트/charset/에셋 예산은 변경하지 않는다.

위 수치는 최종 실행 로그로 확인하며 PR CI 결과는 별도로 확인한다.

실제 GD 사용자 여러 기기 간 맵·음악·SFX 다운로드, 대기열·공통 STARTS IN·Pause/Resume·라운드 전환,
iOS Round 2 기기 안정성, 앱 강제 종료 후 복귀, R2 설치/업데이트는 실기기 확인이 필요하다.
이 작업에서 운영 Render/NeonDB/R2/Apps Script를 수정하거나 플레이 데이터를 생성하지 않는다.

## 운영 반영 순서 (승인 후)

1. PR의 Validate 및 플랫폼 빌드 결과를 확인하고 Debug 실기기로 복수 사용자/봇 혼합 경기를 확인한다.
2. beta68 Debug/Release 패키지를 기존 R2의 불변 버전 경로·descriptor 정책으로 게시한다. 기존 URL을 덮어쓰지 않는다.
3. 서버 beta68 적용 시 기존 migration runner로 0024를 적용하고 schema/health를 확인한다. 요구 버전과 패키지를 동기화한다.
4. 기존 `RANKED_DOWNLOAD_BASE_URL`/채널 descriptor와 GD Relay 설정을 유지한다. 새로운 Worker/버킷/DB 서비스는 필요 없다.
5. Survival을 OFF로 유지한 채 기록·복구·Solo를 확인한 다음 Survival ON으로 온라인 검증한다. OFF는 신규 큐를 취소하고 시작된 경기는 보존한다.
6. 공개 운영 시작 전 실제 8~16기기 네트워크/탈락·재접속·정산을 확인한다. 병합/운영 배포는 이 PR 작성만으로 승인된 것으로 취급하지 않는다.
