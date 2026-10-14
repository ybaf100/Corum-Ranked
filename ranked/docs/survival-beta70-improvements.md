# Survival beta.70 개선 및 Debug SQL 검사

기준 main은 `9b0d6c84fdd20c30ef08981d7505e16cd2de9017`(beta.69)이며 작업 브랜치는 `feat/survival-comprehensive-beta70`이다. 사용자의 마지막 지시에 따라 완성 코드는 main에 반영한다. 서버와 클라이언트 버전은 `v0.5.0-beta.70`이다.

## 구현

1. **랜덤 맵:** 기존 최근 목록 단독 검색을 `MostLiked=2`, `Recent=4`, `Awarded=11`의 카탈로그로 확대했다. GD가 보고한 전체 페이지 범위에서 무작위 페이지를 점진적으로 읽으며, 최대 페이지 인덱스는 199999이다. 난이도·검색 유형별 후보 캐시는 10분/512개이고 동일 ID 후보를 합친다. 선택된 후보는 매번 실제 다운로드의 별·난이도·압축 해제된 geometry·SHA-256을 다시 검증한다. 출시일 제한과 레이팅 등급 가중치는 없다. 전체 Rated 맵에 균등 확률을 보장하지 않는다.
2. **Jukebox:** Solo의 `show()`/`openWhenReady()` 경로를 사용한다. 설치·활성화 확인과 닫기 동작은 공용 팝업에 맡긴다. 살아 있는 팝업은 중복 생성과 자동 PlayLayer 진입을 막으며, 예약된 진입 콜백도 팝업을 다시 확인한다. 라운드/맵 generation이 바뀌면 이전 팝업을 닫는다. 닫은 뒤 기존 준비기가 교체된 음악과 SFX의 준비 상태를 다시 검사한다.
3. **난이도 얼굴:** 검색/다운로드 필드 19와 42를 `featureScore`, `epic`으로 보존한다. `GJDifficultySprite`와 `updateFeatureState`를 사용해 None/Featured/Epic/Legendary/Mythic을 렌더링한다. native 메타데이터 revision이 도착하면 준비 화면을 다시 그린다. 일반/Demon 난이도, 별, 제목, 길이는 유지한다.
4. **음악:** 모든 준비 단계(R1/R2/R3/Final/Tiebreak)는 `Round12`. 탈락 연출은 `ResultLose`, 최종 결과는 서버 `winnerIds`에서 계산한 `result.won`으로 승리/패배 BGM을 고른다. LP 증감은 BGM 판정에 쓰지 않는다. 연출 중 서버가 다음 단계로 넘어가도 결과 음악을 유지하고 연출 종료 후 준비 음악으로 전환한다. 음악 모드가 바뀔 때만 재생 요청해 매 프레임 같은 곡을 재시작하지 않는다. 실제 플레이는 기존 native GD 음악 소유권을 유지한다.
5. **개인 최고 기록:** 서버의 현재 라운드 최고 기록을 Color 1 세로선으로 표시한다. 부드러운 표시용 보간을 사용하고 match/round/map generation 변경 시 초기화한다. 개인 선은 상단 캡, 커트라인/1위 선은 서로 다른 하단 캡을 사용해 같은 위치에서도 구별한다. native 바의 전체 길이·좌표 변환을 사용하고 바 OFF 설정을 존중한다. Death Match 위치는 소수점 세 자리 기록을 사용한다.
6. **정확한 봇 수:** 대기열 Fill은 0–15명, 별도 Debug는 1–15명(기본 15명)이다. 총 인원은 2–16명이어야 한다. 서버 admission transaction에서 사람 수를 다시 확인하며 요청한 봇 수를 조정하지 않는다. 즉시 경기 생성 후 기존 5초 소개·맵/음악 준비·native 입장·카운트다운을 따른다. 2–3명은 Final, 4–7명은 Round 3→Final, 8–16명은 기존 축소 경기 컷/LP 대응식을 사용한다. 별도 Debug의 `variableSize`는 온라인 연결 종료/항복 정책을 켜지 않는다. Force n은 실제 전체 인원까지만 허용한다.
7. **Debug SQL 검색:** Solo 실행은 기존 설정(기본 125ms), Survival 실행은 250ms를 유지한다. 활성/미정산 경기 전체 검색만 2초로 분리했다. 시작 시 즉시 복구하고 새 경기 생성 직후 메모리에 등록한다. 검색은 중첩하지 않고 검색 실패 시 기존 목록을 유지한다. FINAL_RESULT는 DB의 `settled_at` 확인 후 제거한다. LP 지급 뒤 결과 저장이 실패하면 다음 tick 또는 재시작 후 ledger의 동일 event key로 복구한다. 공통 Survival 복구 루프, 일반 경기 polling, 봇 알고리즘/갱신 시각, heartbeat와 정산 모델은 변경하지 않았다.

추가로 일반 Survival의 순위/컷/탈락 점수는 최고 진행률의 정수 부분으로 판정한다. `50.001%`와 `50.9999%`는 모두 50점 공동 순위다. Attempt 증거 원본에는 소수 진행률을 보존하고 Death Match만 기존 .001% 비교를 사용한다.

Admin `RESET_DB`의 Solo/Survival은 각 모드의 프로필·경기·정산 기록을 초기화한다. Common은 두 모드와 공통 도전과제/AP/칭호를 초기화한다. 계정과 세션은 유지하며 진행 중인 경기/미정산 경기가 있으면 transaction을 거절한다. 이번 수정에서 운영 DB 전체 초기화는 실행하지 않았다.

## DB 및 API

- `0026_survival_catalog_bot_count.sql`: `sur_map_selection_history`와 최근 이력 인덱스, Fill 요청의 nullable `bot_count` 추가. SQL 최적화를 위한 스키마 변경은 없다.
- `POST /api/ranked/survival/queue/fill-bots`: optional `botCount: 0..15`. 생략 시 기존 총 16명 채우기 동작을 유지한다. 동일 requestId의 봇 수 변경은 conflict.
- `POST /api/ranked/survival/debug/matches`: optional `botCount: 1..15`, 기본 15. 평균 LP 생성·Force 범위·정산을 실제 인원으로 계산한다. 동일 requestId의 인원 변경은 conflict.
- map certificate에 optional `length`, `featureScore`, `epic`; 신규 파서는 length를 항상 보존한다. legacy snapshot의 누락된 length는 native 데이터로 보완하되 native XL/Platformer는 거절한다.
- Debug/온라인 결과에 `won` 추가. 저장된 예전 결과는 클라이언트의 기존 1위 fallback을 사용할 수 있다.

## 최근 30회 선정 이력

온라인과 Debug가 같은 전역 이력을 사용한다. 검색 및 다운로드는 DB 잠금 밖에서 수행한다. 확정 transaction에서 매치/작업 token/generation을 재확인하고 전역 advisory lock을 얻어 최신 이력을 검사한다. 경기 map 저장과 이력 INSERT가 함께 commit된다. 충돌 후보는 재검색하며 실패하거나 stale한 후보는 기록하지 않는다. 같은 매치의 round/generation은 한 번만 기록한다.

검증된 후보 풀이 소진된 경우에만 전역 중복 창을 30→20→10→0회로 완화한다. ID≥40M, Rated, 길이 0–3, Auto/Platformer 제외, 라운드 난이도, 같은 경기에서 이미 사용한 맵 제한은 완화하지 않는다. API/다운로드 실패를 후보 부족으로 처리하지 않는다. 이력은 DB에 남아 서버 재시작 후에도 유지된다.

## SQL 측정

[원본 측정값](survival-debug-sql-beta70.json)은 실제 PGlite SQL executor에서 `query`와 transaction 내부 `query`를 계수한 값이다. 단순 검색 함수 호출 수가 아니다. BEGIN/COMMIT, HTTP 요청과 공통 복구 worker의 SQL은 제외했다. 4초의 제어된 서버 시계 구간에서 같은 실행 주기로 비교했으며 측정 중 경기는 INTRO/MAP_SELECTION 상태였다. 변경 전 비교는 같은 엔진/SQL executor에 이전의 매-tick 검색 빈도를 재현했다. 운영 DB의 네트워크 지연이나 서버 전체 SQL 감소율을 뜻하지 않는다.

| 모드 / 활성 경기 | 활성 검색 SQL/s 전→후 | Debug 관련 전체 SQL/s 전→후 | 전체 감소율 |
| --- | --- | --- | --- |
| Solo / 0 | 8 → 0.5 | 8 → 0.5 | 93.75% |
| Solo / 1 | 8 → 0.5 | 168 → 160.5 | 4.46% |
| Solo / 3 | 8 → 0.5 | 488 → 480.5 | 1.54% |
| Survival / 0 | 4 → 0.5 | 4 → 0.5 | 87.5% |
| Survival / 1 | 4 → 0.5 | 16 → 12.5 | 21.875% |
| Survival / 3 | 4 → 0.5 | 40 → 36.5 | 8.75% |

활성 검색 감소율은 Solo 93.75%, Survival 87.5%. 실행 timer의 125ms/250ms 값과 검색 사이 각 실행 tick을 별도로 검증했다. 새 경기 등록은 생성 요청 안에서 완료되어 추가 2초 대기가 없다. 측정한 생성 요청 시간은 Solo 86.59ms, Survival 65.49ms였고, 지급 후 프로세스 재시작을 재현한 정산 재시도는 다음 250ms tick에서 완료됐다(해당 DB 처리 15.04ms). 지급 횟수는 1회였다. 이 지연값은 로컬 단일 샘플이며 변경 전 ms 지연과 운영 지연은 측정하지 않았다. 검색 오류 주입 후 목록 유지·재조회 복구, 미정산 복구와 중복 지급 방지도 통과했다.

## 검증

- Rules: 235개 통과. 일반 점수 공동 순위와 Death Match 정밀도 포함.
- 서버: 전체 519개 실행 후 버전 기대값 2개와 SQL 계수 테스트의 실행 제한 시간 1개를 수정했다. 추가 버전 회귀를 포함한 관련 파일 89개 재실행 통과; 최종 검사 대상은 520개, 남은 실패 0개. 정산 후 재시작/중복 지급 검사는 별도로 재실행해 통과했다.
- PostgreSQL 16 실제 연결 + HTTP: 온라인·동시 매칭·지정 봇 수·공통 맵 이력 확정·재접속·LP 정산 40개 통과. PGlite 단일 연결 검사와 별개로 수행했다.
- native 경계/도메인 회귀: JavaScript/C++ 경계 검사 180개(전체 179개와 추가 폰트 회귀)와 strict-warning domain 실행 통과. 최신 수정에 해당하는 native 8개 재실행 통과.
- 전체 TypeScript 타입 검사/서버 빌드 통과. Apps Script 설정/기존 기록 호환, Worker 13개, 패키징/게시/asset-pack 검사 16개 통과.
- Geode 5.8.2 / GD 2.2081 / NDK r28c Android64: Debug ON C++ 전체 컴파일·AArch64 shared-library 링크 통과. Debug OFF C++ 전체 컴파일·링크도 통과했다. 전체 binary 결합 패키지/실기기 검증은 하지 않았다. 폰트 생성물은 아래 별도 검사로 검증했다.
- Windows/iOS 호스트·SDK와 실제 GD 기기는 없어 해당 플랫폼 빌드·Jukebox 외부 UI·FMOD 출력·다양한 해상도 시각 확인은 미검증이다. iOS JIT-Less 충돌 관련 후킹/텍스처 수명 코드는 수정하지 않았다.
- 검색 type 값은 실제 Geode enum(2/4/11)과 GD 프로토콜/페이지 fixture, 인증 Worker pipeline으로 검증했다. 이 작업 환경의 GD 직접 POST는 HTTP 403으로 차단되어 실제 GD 검색 유형별 페이지를 직접 조회하지 못했다. 전시대 균등 추첨이나 운영 GD 검증 완료를 주장하지 않는다.

## 운영 반영

1. main의 beta.70 서버 배포 시 기존 startup migration runner가 0026을 적용한다. 먼저 서버 로그에서 migration 성공을 확인한다.
2. 기존 `corum-ranked-gd-relay` Worker에 [업데이트된 worker.mjs](../tools/gd-relay/worker.mjs)를 배포한다. 기존 Secret/Render relay origin은 유지한다. 신규 검색은 `type=2/4/11`, `len=0,1,2,3`, page 0–199999이다. 별도 R2 Worker가 아니다.
3. 구형 type-4 Worker가 아직 배포되어 있으면 서버가 구형 검색 형식으로 재시도하고 XL은 로컬 검증에서 제외한다. 경기는 계속 선택할 수 있지만 넓은 페이지·다중 카탈로그 검색은 Worker 업데이트 후 적용된다.
4. 플랫폼 CI 산출물을 기존 R2의 불변 버전 경로로 게시한다. 실제 GD에서 준비/Jukebox/음악/마커/짧은 경기·온라인 복귀를 확인한다.

이번 작업은 운영 Worker/R2/Apps Script를 수동 배포하지 않으며 추가 운영 DB 초기화도 수행하지 않는다. main 푸시와 PR 병합은 사용자가 승인했다.


## 추가 패키징 오류 수정

beta.69의 [통합 Debug package](https://github.com/ybaf100/Corum-Ranked/actions/runs/38512350355/job/115759920746)와 iOS Debug OFF 검사는 컴파일 이후 `Combined font atlas pixels exceed the 96 MiB RGBA budget`로 실패했다. Geode CLI 3.9.0으로 같은 문자셋을 생성해 UHD 98.77 MiB 초과를 재현했다.

현재 UI에서 사용하지 않는 Regular(`CorumSans`)의 중복 한글 glyph만 제거하고 Latin/기호를 유지했다. 실제 UI에서 사용하는 Medium/SemiBold/Bold/Black과 큰 카운트다운의 font size, glyph, 이미지·메트릭은 그대로다. 해당 30개 생성 파일이 변경 전과 byte-identical임을 확인했다. 이 Regular 리소스가 한글 UI에 새로 사용되면 필요한 glyph와 아틀라스 한도를 다시 검토하도록 소스 회귀 검사를 추가했다.

18개 실제 생성 SD/HD/UHD font bundle을 기존 검증기에 넣어 통과했다. UHD 합계는 98.77→83.84 MiB로 줄었고 96 MiB 한도는 유지했다. 폰트/팝업/항복/HUD 회귀 16개도 통과했다. [원본 아틀라스 측정](font-beta70-atlas.json). 이 검사는 폰트 생성물 검사이며 Android/iOS 기기 안정성 검증은 아니다.
