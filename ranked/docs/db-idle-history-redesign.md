# DB 유휴 처리와 경기 기록 개편

기준 main: `7fa925904175a22722d07d6c14827f3cc0eed930` (2026-10-24 fetch 재확인). 작업 브랜치: `feat/db-idle-history-20261024`. PR: <https://github.com/ybaf100/Corum-Ranked/pull/32>.

버전은 `v0.5.0-beta.95`를 유지한다. 이 PR은 서버/클라이언트 소스 변경이며, 운영 배포나 새 클라이언트 버전 활성화를 수행하지 않는다. 게임 서버는 Lightsail Singapore, DB는 Neon Singapore, Railway는 update-only bridge인 기존 구조를 따른다.

## DB 활성화 원인과 변경

조회 간격을 늘리는 대신, 작업의 커밋과 서비스 시작을 활성화 신호로 사용한다. `DemandWorker`는 시작 시 한 번 durable 상태를 조정하고, 작업이 없으면 interval을 해제한다. 신규 작업을 저장한 트랜잭션의 **COMMIT 이후**에 해당 도메인을 깨운다. COMMIT 전 ROLLBACK은 깨우지 않는다. COMMIT 응답 연결이 유실되면 실제 저장 여부가 불명확하므로 일회성 조정을 요청하며, 미확정 afterCommit 후처리 콜백은 실행하지 않는다. 빈 조회 중 새 작업이 커밋되어도 generation 비교로 wakeup이 유실되지 않는다. DB 오류가 발생하면 활성 주기로 재시도하며, 성공한 빈 조회만 유휴 전환을 허용한다. 단일 authoritative writer 구조를 전제로 한다.

| 경로 | 기존 SQL/타이머 | 변경 후 |
| --- | --- | --- |
| SurvivalEngineService | 활성 여부와 무관하게 250ms마다 복구 검색 | 시작/Survival 생성 커밋에 활성화. 활성 roster가 있는 동안 250ms 유지, 빈 roster이면 중단 |
| SurvivalOnlineService | 500ms마다 online roster 검색 | 활성 경기 또는 미완료 정산이 있는 동안만 500ms 유지 |
| SurvivalQueueService | 빈 대기열에서도 500ms마다 정리/검색 | 참가/퇴장/heartbeat/fill 커밋에 활성화. QUEUED가 없으면 중단 |
| SurvivalDebugService | 500ms driver와 별도 2초 discovery | 시작/신규 경기 커밋에서만 discovery. 실제 경기/정산만 500ms driver 유지 |
| DebugBotService | 설정 tick마다 driver와 약 2초 discovery | 시작/신규 Debug 경기 커밋에서만 discovery. 실제 driver가 있을 때만 설정 tick 유지 |
| DiscordRelayWorker | 빈 outbox도 pollMs마다 claim | 시작/enqueue 커밋에서 활성화. 미전송·재시도·lease 작업이 없으면 중단 |
| SurvivalConfigService | 설정 갱신 때 mode UPSERT/청소 반복 | 동일한 내용은 SQL 생략. OFF 전환은 대기열 청소를 커밋한 뒤 공개 |
| AppsScriptRankedConfigSource/RankedMapMetadataResolver | 매 갱신마다 DB cache SELECT, TTL 만료 시 UPSERT | 시작·실제 원본 설정 변경 시만 DB 허용. 같은 원본의 외부 HTTP TTL 갱신은 메모리만 갱신 |
| RankedConfigService | OFF인 설정을 읽을 때 대기열 청소 반복 | 시작 OFF 또는 enabled→OFF일 때만 청소. 외관 metadata generation 변경은 청소하지 않음. 기존 MATCHED 유지 |
| DebugSpectateService | 샘플 interval | 유휴 tick은 메모리 map만 사용. SQL 없음 |
| SessionService/SessionGuard | 요청 시 인증·세션 SQL | 기존 요청 기반 동작 유지. 무사용 상태의 세션 감시 SQL 추가 없음 |
| MatchRecoveryService | 서버 시작 시 Solo 복구 | 기존 시작 복구 유지. 별도 상시 DB watchdog 없음 |

Apps Script는 generation이 같아도 매 응답마다 `generatedAt`을 새로 만든다. 이 필드는 내용 fingerprint에서 제외한다. generation만 비교하지 않으므로 같은 generation의 실제 OFF/내용 변경도 적용되며, 읽은 시각만 달라지는 응답은 DB를 깨우지 않는다. 설정 HTTP 갱신과 맵 metadata의 외부 HTTP TTL 갱신은 계속 수행한다. 원본 문서가 같으면 영속 metadata cache의 SELECT/UPSERT를 생략한다. 운영자가 원본 설정을 바꾸거나 프로세스가 재시작하면 DB cache를 다시 사용할 수 있다.

미완료 경기, outbox lease, 정산 projection은 DB에 남아 재시작 시 복구한다. 완료 라운드·컷오프·봇·Attempt·LP 처리는 기존 엔진/정산 코드를 사용한다. 마지막 10초 체크포인트와 250ms 권위 타이머를 느리게 바꾸지 않았다. 수동 SQL로 새 업무 행을 추가하거나 다른 writer를 도입할 경우 이 프로세스의 commit 신호를 우회하므로, 별도 통합 또는 서버 재시작 복구가 필요하다.

### 실제 SQL 계측

동일한 PGlite/PostgreSQL SQL executor를 쓰는 전체 Nest `AppModule` 테스트에서 모든 DB query와 transaction 내 query를 계수했다. 사용자·경기·대기열·outbox 0개이며 Debug와 relay 기능은 켜 둔다. 앱 시작 완료 후 **가상 서버 시계·interval 시간 360초**를 진행한다. 설정 응답은 매번 `generatedAt`만 달라지도록 하여 실제 Apps Script 동작을 모사한다. 설정 소스/메타데이터 resolver는 실제 구현이고, 외부 HTTP만 mock한다. 계측을 위한 DB 조회를 추가하지 않는다.

| 구간 | 기준 main | 변경 후 |
| --- | ---: | ---: |
| 시작 조정 SQL 호출 | 16 | 23 |
| 이후 유휴 360초 SQL 호출 | 5,784 | 0 |
| 유휴 SQL/초 | 약 16.067 | 0 |

시작 시 일회성 복구를 더 명확히 기다리므로 시작 조정 건수는 증가한다. 유휴 검증은 벽시계로 운영 Neon을 6분 관측한 결과가 아니다. 기존 main에 동일한 계측 테스트를 적용한 별도 worktree와 변경 worktree에서 측정했다.

유휴 후 일반 로그인 → Survival 대기열 참가 → 500ms 처리 재개 → 퇴장 → 추가 360초 SQL 0회도 검증했다. 활성 1/3개 Debug 경기 테스트는 discovery를 매 tick 강제하는 비교군과 커밋 기반 구현을 비교한다. 4초 구간에서 Solo 125ms tick의 불필요한 discovery 32회를 0회로, Survival 250ms 직접 엔진 테스트의 discovery 16회를 0회로 줄이면서 실제 driver 처리를 유지한다. 실제 네트워크 환경의 경기 지연이나 프레임 성능을 계측한 결과는 아니다.

Neon Scale to Zero가 5분 SQL 유휴를 확보할 수 있는 코드 경로가 된다. `suspend_timeout_seconds=0`을 절전 비활성으로 해석하거나 설정을 변경하지 않는다. **운영 Neon의 실제 suspend/resume 성공은 미검증**이며 이 PR의 코드상 SQL 0회 결과와 구별한다.

### 연결과 헬스체크

`pg` runtime pool의 `idleTimeoutMillis=30000`, migration pool의 5000ms, idle error listener, reconnect 동작을 유지한다. TCP keepalive는 SQL ping이 아니다. 5분 유휴 전에 runtime 연결이 반환되므로 첫 신규 요청은 새 연결을 열 수 있다.

격리된 로컬 PostgreSQL에서 전용 backend를 종료한 뒤 pool이 오류를 처리하고 재연결하는 것, 30초 후 idle connection 수가 0이 되는 것, 재연결 전후 커밋된 receipt가 같은 값을 유지하는 것을 실행 검증했다. 이 테스트는 localhost 테스트 DB에만 허용한다. Production backend를 종료하지 않는다.

- `/health`: SQL 없음. Lightsail Docker HEALTHCHECK 30초 주기와 activation 스크립트도 `/health`를 사용한다.
- `/ready`: 기존 실제 DB ping + schema 검사 유지. 호출하면 DB를 깨울 수 있다.
- Caddy 예제에는 `/ready` 주기 probe가 없다. 저장소의 상시 `/ready` 모니터 설정도 발견되지 않았다.
- `/api/ranked/config`의 audio volume 읽기는 요청 시 SQL을 사용한다. 실제 클라이언트·외부 bridge/config 요청이 계속되면 별도 사용자/외부 트래픽으로 DB가 활성화될 수 있다. 서버 내부 주기 config HTTP 갱신 경로와 구분한다.
- 저장소 밖의 모니터링 서비스/수동 운영 설정 전체는 확인하지 못했다. 배포 후 운영 트래픽 로그에서 `/ready` 주기 요청을 별도로 확인해야 한다.

## 마이그레이션과 도전과제 보존

작업 시작 시 운영 0020/0029 적용 기록을 읽기 전용으로 확인했다. REQUIRED_MIGRATIONS의 0020 및 `schemaStatus()`의 `native_death` 검사를 유지한다. 새 SQL migration, ALTER TABLE, epoch 갱신 또는 도전과제 초기화를 추가하지 않는다. beta.95의 48종, 마지막 10초·공동 1위·Death Match·Hidden 판정, 활성화 epoch, AP ledger/중복 방지, V/Mythic 색상·진행도 효과는 유지한다.

도전과제 카드/상세 제목/해금 알림은 실제 이름만 표시한다. 장착 칭호·프로필·칭호 선택에는 scope 접두사를 유지한다. 내부 구분은 ID/scope이며 같은 한국어 이름으로 합치지 않는다. 해금 전 Hidden은 로컬 카탈로그의 진짜 이름/조건을 조회하지 않는다.

## 기록 API 계약

목록: `GET /api/ranked/history?scope=ALL|SOLO_RANKED|SURVIVAL&summary=true&order=NEWEST|OLDEST&result=ALL|WIN|LOSS|CHAMPION|PODIUM|ELIMINATED|IN_PROGRESS`.

상세: `GET /api/ranked/history/SOLO_RANKED/:matchId` 또는 `.../SURVIVAL/:matchId`. 모두 SessionGuard를 사용한다. 상세는 해당 플레이어 소유와 history visibility epoch를 SQL에서 확인하며, 없는/숨김/타인 경기는 404다. UUID·정렬·필터·추가 identity query를 검증한다. 필터/정렬을 각 모드의 SQL LIMIT 30 전에 적용한 후 혼합 목록도 30건으로 제한한다. 진행 중 Survival은 먼저 배치하고, 완료 경기는 종료 시각으로 정렬한다.

- 목록은 요약만 요청한다. 상세는 선택한 경기만 지연 조회한다. 완료된 상세는 화면 세션 내에 캐시한다.
- 완료된 라운드만 `completed=true`, standings/cutoff/통과 인원/ownQualified를 제공한다. 미완료 round는 standings가 비어 있고 cutoff/qualifiedCount는 null이다.
- Survival `inProgress=true`이면 개인의 조기 LP 지급 여부와 관계없이 최종 `result/finalRank/finishedAt`은 null이다.
- 경기 전체가 끝났지만 개인 payout projection이 없으면 `settlementPending=true`; 최종 순위와 LP 미확정을 구분한다.
- 원본 aggregate의 checkpoint 증거·driver·설정·미종료 Attempt는 공개하지 않는다. legacy snapshot/result는 필요한 표시 필드만 whitelist한다.
- summary에서도 확정된 통과 인원은 실제 행으로 계산한다. 같은 컷오프 공동 순위가 목표 인원을 넘는 규칙을 보존한다.
- Solo는 4개 batch query로 페이지의 일반 라운드/DM/유효 종료 Attempt 통계를 구한다. scoreA/B는 누적 점수로 유지하며 최고 진행도와 혼용하지 않는다.
- 최고 진행도/clear는 `valid AND ended_at IS NOT NULL`인 실제 Attempt에서 계산한다. 기록이 없으면 null로 표시한다. DM은 3자리 소수와 실제 TIE→다음 DM 연결을 유지한다.
- Solo 맵은 저장된 playable ID, Survival은 확정 map certificate의 ID를 사용한다. canonical ID나 이름으로 누락된 playable ID를 추측하지 않는다.
- 티어는 당시 rating projection만 사용하며, 현재 프로필 티어를 과거 티어로 대체하지 않는다.
- 이후 생성되는 Survival map snapshot은 검증된 맵 이름/creator도 보관한다. 과거에 없던 맵명/길이/등급은 생성하지 않는다.

## 클라이언트 기록 화면

공통 다크 카드, 모드 탭, 최신/오래된 순, 모드별 유효 결과 필터, 경기 상세, 실제 round/DM 탭을 사용한다. Placement의 실제 단계 점수/등급 카드는 별도로 보존한다. 모든 상세를 목록에 펼치지 않는다.

Solo는 YOU/상대의 시리즈 점수·당시 티어와, 선택 라운드 양측 최고 진행도·clear·라운드 결과를 분리한다. Survival은 넓은 화면에서 맵/전체 순위/내 기록 3영역, 좁은 화면에서는 세로 스크롤을 사용한다. 내 행·공동 순위·컷오프·통과/탈락·100%를 구분한다. 없는 라운드는 만들지 않는다. 미완료 라운드는 진행 중 안내만 표시한다.

화면의 새로고침 버튼으로 갱신한다. 무한 history polling은 없고, 닫기/탭 변경/세션 교체 시 요청을 취소한다. generation으로 이전 응답을 거부하고 동일 상세 중복 요청을 막는다. 완료 상세의 자동 재조회는 없다. 모드·경기·라운드·목록 스크롤을 native 상세 복귀 시 유지한다.

카드가 전체 면적에서 터치되므로 리더보드의 검증된 ScrollLayer 입력 소유자를 재사용한다. 카드 위에서 시작한 drag도 원래 손가락으로 스크롤하고, 움직이지 않은 탭만 아직 유효한 경기로 전달한다. 행별 swallowing CCMenu를 사용하지 않는다. 썸네일은 기존 bounded HTTP/decoder/disk cache를 재사용하는 별도 history owner로 처리하여 실시간 Survival reveal을 취소하지 않는다. 닫을 때 context를 해제하며 history owner는 random thumbnail pool을 요청하지 않는다.

## 맵 보기와 native 수명

`맵 보기 / VIEW LEVEL`은 정확한 ID의 saved metadata 또는 GD의 keyed exact-ID 조회를 거쳐 `LevelInfoLayer::scene(level,false)`를 push한다. Play는 사용자가 GD 기본 Play 버튼으로 선택한다. 노래 준비 gate/Ranked Ready/Attempt 생성/공식 결과 송신을 호출하지 않는다.

process-lifetime lookup owner와 keyed response routing을 사용한다. 요청 중 전역 delegate를 점유하지 않고, 해당 응답을 처리할 때만 빌린 뒤 기존/새 foreign delegate를 보존한다. 취소된 key도 제한된 목록으로 처리해 다른 화면에 이전 결과를 전달하지 않는다. 다른 ID는 거부한다. timeout/누락/삭제된 맵은 안내한다. native scene을 만들기 전에 level Ref를 유지하고, popup/scene은 WeakRef 및 현재 scene 검사로 유효성을 확인한다.

Solo 매칭/경기, Survival 활성/생성/대기열/준비, Debug Spectate 활성, 기존 PlayLayer 중에는 열지 않는다. 기존 PR #30의 native texture-cache 보유와 정확한 native-return 경로는 유지하며, 임의로 PlayLayer를 detach하지 않는다.

## 추가 요청: Survival 후광·자동 복귀·100% 재입장

제공된 로그 3개는 모두 beta.94 설치본이다. `Geode 2026-10-24 00.58.55.log`의 01:26:34 부근에는 서버 결과 도착 뒤에도 다음 상태가 반복된다.

```text
[SurvivalExit] queued=false running=true retired=true initialized=true sceneOwned=true nextScene=false
```

기존 cleanup이 `safetyRetired`로 차단되는 코드와 일치한다. retirement가 먼저 설정된 정확한 native 호출 원인까지 로그만으로 단정하지 않는다. 수정은 scoring retirement를 해제하지 않고, 여전히 현재 scene을 소유하는 그 PlayLayer의 종료만 허용한다. process heartbeat도 같은 종료 경로를 사용하여 Pause로 로컬 scheduler가 멈춰도 서버의 항복 ACK·라운드 확정·탈락 뒤 native Pause Quit/완료 overlay를 거쳐 복귀한다. 새 scene/교체된 PlayLayer/진행 중 transition은 건드리지 않는다.

난이도는 native GJDifficultySprite의 canvas/frame과 feature 자식의 원래 offset을 유지한다. 프레임 교체 후 모든 ornament를 임의로 중앙으로 옮기던 처리를 제거했다. pinned bindings의 Featured/Epic/Legendary/Mythic enum과 native level metadata를 사용하고, 선택 패널은 중복 외부 난이도 글자를 생략한다.

100%는 서버의 종료 Attempt ACK 후 확정한다. 현황판의 재입장 버튼은 최신 상태를 한 번 확인한 후 같은 맵의 기존 `armClearedReplay` 로컬 재플레이를 연다. 공식 Attempt·LP·도전과제를 추가하지 않으며, 재플레이 종료 후 반복 재입장이 가능하다. 재입장은 명시적 클릭으로만 요청하고 다음 라운드로 의도를 이월하지 않는다.

## 검증과 배포 전 확인

로컬 실행 결과: rules 282개, 서버/DB 597개 전체 통과 후, settlement/legacy projection 추가 테스트 6개 통과(그중 1개 신규). 서버 build와 source/test typecheck 통과. PostgreSQL 실제 row lock suite, native_death 흐름, 마지막 10초 체크포인트·재시작·정산 중 장애 회귀를 포함한다. 전체 앱 유휴/재활성화 테스트도 통과했다.

후속 COMMIT 응답 유실/일반 rollback/정상 commit 테스트 3개도 추가했다.

네이티브 회귀는 SDK JSON을 링크한 history model/transport, exact-ID/delegate/Ref 수명, 카드 스크롤·요청 취소, Pause 퇴장, 100% 재입장, 기존 PR #30 계열 preparation 검증을 실행한다. 최종 CI/네 플랫폼 결과는 PR 실행 결과로 갱신한다. 로컬 Linux에는 실제 GD/Geode 기기가 없으므로 iPad/iPhone/Android/Windows의 실제 UI·터치·GPU·게임 전환을 검증 완료로 표시하지 않는다.

배포 전에는 다음을 별도로 확인한다.

1. PR 최종 SHA의 서버 CI와 Release/Debug Windows/iOS/Android32/Android64 빌드.
2. 격리 환경의 일반/Debug Solo·Survival, 재접속, 항복, 100% 후 반복 재입장, 컷오프/DM 공동 순위.
3. 실기기의 원본/삭제/미다운로드/ID 누락 맵, 맵 상세 Play 및 Back, 한국어 긴 이름·작은 해상도.
4. Featured/Epic/Legendary/Mythic 후광과 난이도별 native 표시.
5. 승인 후 Singapore만 배포. `/health`와 실제 `/ready` 확인을 구별하고 rollback·known_hosts·단일 writer를 유지.
6. 배포 후 무사용 구간의 실제 서버 쿼리/외부 `/ready` 트래픽과 Neon Compute suspension을 수동 자료로 확인. 측정을 위한 주기 SQL을 새로 넣지 않는다.
7. 새 클라이언트 버전을 낼 경우 메타데이터를 함께 올리고 immutable R2 공개를 먼저 확인한 뒤 요구 버전을 별도 활성화. 이 PR 자체는 기존 패키지를 덮어쓰거나 버전을 강제하지 않는다.

## 변경 파일

아래 목록은 기준 main 대비 이 PR의 소스·테스트·번역·문서 파일이다.

- `corum-ranked-mod/mod.json`
- `corum-ranked-mod/src/HistoryLevelLookup.cpp`
- `corum-ranked-mod/src/HistoryLevelLookup.hpp`
- `corum-ranked-mod/src/PlayLayer.cpp`
- `corum-ranked-mod/src/RankedLevelPreparation.cpp`
- `corum-ranked-mod/src/RankedPopup.cpp`
- `corum-ranked-mod/src/RankedRuntime.cpp`
- `corum-ranked-mod/src/RankedRuntime.hpp`
- `corum-ranked-mod/src/SurvivalThumbnailProvider.cpp`
- `corum-ranked-mod/src/SurvivalThumbnailProvider.hpp`
- `corum-ranked-mod/src/domain/AchievementPresentation.hpp`
- `corum-ranked-mod/src/domain/HistoryPresentation.hpp`
- `corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp`
- `corum-ranked-mod/src/ui/AchievementUnlockToast.cpp`
- `corum-ranked-mod/src/ui/MapDifficultyBadge.hpp`
- `corum-ranked-mod/src/ui/RankedLeaderboardPopup.cpp`
- `corum-ranked-mod/src/ui/RankedTapScroll.hpp`
- `corum-ranked-mod/src/ui/SurvivalGameLayer.cpp`
- `corum-ranked-mod/tests/AchievementOverhaulNativeTest.mjs`
- `corum-ranked-mod/tests/HistoryLevelLookupNativeTest.mjs`
- `corum-ranked-mod/tests/HistoryNativeTest.mjs`
- `corum-ranked-mod/tests/HistoryPresentationNativeTest.mjs`
- `corum-ranked-mod/tests/MapDetailsNativeTest.mjs`
- `corum-ranked-mod/tests/RankedInputNativeTest.mjs`
- `corum-ranked-mod/tests/RankedLobbyLifecycleNativeTest.mjs`
- `corum-ranked-mod/tests/SpectatorAudioNativeTest.mjs`
- `corum-ranked-mod/tests/SurvivalPauseNativeTest.mjs`
- `corum-ranked-mod/tests/SurvivalUiNativeTest.mjs`
- `ranked/apps/server/src/config/map-metadata.ts`
- `ranked/apps/server/src/config/ranked-config.service.ts`
- `ranked/apps/server/src/config/ranked-config.source.ts`
- `ranked/apps/server/src/config/survival-config.service.ts`
- `ranked/apps/server/src/database/database-work.ts`
- `ranked/apps/server/src/database/database.port.ts`
- `ranked/apps/server/src/database/database.service.ts`
- `ranked/apps/server/src/debug-bot/debug-bot.service.ts`
- `ranked/apps/server/src/history/history-options.ts`
- `ranked/apps/server/src/history/history.controller.ts`
- `ranked/apps/server/src/history/history.service.ts`
- `ranked/apps/server/src/history/solo-history.ts`
- `ranked/apps/server/src/history/survival-history.ts`
- `ranked/apps/server/src/match/match.service.ts`
- `ranked/apps/server/src/queue/queue.service.ts`
- `ranked/apps/server/src/relay/discord-relay.worker.ts`
- `ranked/apps/server/src/relay/outbox.service.ts`
- `ranked/apps/server/src/survival/survival-debug.service.ts`
- `ranked/apps/server/src/survival/survival-engine.service.ts`
- `ranked/apps/server/src/survival/survival-map.provider.ts`
- `ranked/apps/server/src/survival/survival-online.service.ts`
- `ranked/apps/server/src/survival/survival-queue.service.ts`
- `ranked/apps/server/src/survival/survival.service.ts`
- `ranked/apps/server/test/commit-recovery.test.ts`
- `ranked/apps/server/test/database-work.test.ts`
- `ranked/apps/server/test/debug-bot.integration.test.ts`
- `ranked/apps/server/test/history-projection.test.ts`
- `ranked/apps/server/test/history.test.ts`
- `ranked/apps/server/test/idle-app.integration.test.ts`
- `ranked/apps/server/test/pglite-database.ts`
- `ranked/apps/server/test/pool-reconnect.integration.test.ts`
- `ranked/apps/server/test/ranked-config-idle.test.ts`
- `ranked/apps/server/test/sql-counter.ts`
- `ranked/apps/server/test/survival-debug.integration.test.ts`
- `ranked/apps/server/test/survival-engine.integration.test.ts`
- `ranked/apps/server/test/survival-online.integration.test.ts`
- `ranked/docs/db-idle-history-redesign.md`
- `ranked/packages/rules/src/survival-maps.ts`
- `ranked/translations/README.md`
- `ranked/translations/ui.json`
