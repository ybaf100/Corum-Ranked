# Survival 16/64-player operating format

기준 main: `196292ea19934b528cab45d972ff2a9a40594797`. 사용자가 구현 완료 후 패키지 게시와 Singapore 운영 배포까지 승인했다. 새 클라이언트 버전은 beta.97이다.

## 경기 규칙

| 운영방식 | 시작 인원 | R1 통과 목표 | R2 | R3 | R4 | 결승 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 16인 | 16 | 10 | 6 | 3 | 없음 | 1 |
| 64인 | 64 | 40 | 26 | 12 | 5 | 1 |

숫자는 서버의 통과 **목표**다. 기존 컷오프 동점자는 모두 통과하므로 실제 생존 인원은 목표를 초과할 수 있다. 결승 동점은 기존 3회 Attempt, 소수점 3자리 TIEBREAK로 결정한다. 라운드 타이머, 마지막 10초 체크포인트, 서버 권위 기록과 정산은 공통 엔진을 사용한다.

기존 180초 대기 후 소규모 경기와 명시적 봇 수 선택을 유지한다. 64인 운영방식의 작은 필드는 인원 비율에 따라 목표를 축소하고, 소규모 결승 흐름을 유지한다. 64인 Debug는 최대 63봇과 1~64위 강제 순위를 지원한다. 봇은 계정이나 플레이어 보상을 받지 않는다.

R4의 맵 자격 범위는 같은 난이도 그룹의 기존 R3 범위를 재사용한다. 결승/TIEBREAK 범위는 유지한다. 클라이언트 다운로드 인증과 서버 검증이 같은 표를 사용한다.

## 어드민 설정과 보존

어드민 창 → SURVIVAL → **서바이벌 운영방식**에서 현재 설정을 읽고 16인/64인을 선택한다. 기존 로그인 세션, `hwanhee1` 이름 및 Debug 비밀번호 검증을 모두 거친다. 서버 API는 `GET_SURVIVAL_MODE` / `SET_SURVIVAL_MODE`와 숫자 `matchPlayerCount: 16 | 64`만 허용한다.

설정은 `sur_mode_state.match_player_count`에 저장하며 서버 재시작 후 복원한다. 어드민 변경·원격 설정 갱신·대기열 생성은 같은 admission fence와 PostgreSQL advisory lock을 사용한다. 새 설정은 COMMIT 확인 후 메모리에 반영한다. COMMIT 응답 유실 시 다음 설정 갱신에서 한 번 다시 읽고 조정한다. 같은 원격 설정의 주기 갱신은 추가 SQL을 만들지 않는다.

대기열은 새 운영방식으로 매칭한다. 이미 생성된 경기·재접속·idempotent 생성 재요청은 저장된 config snapshot을 유지한다. 운영 OFF 전환의 대기열 비활성화와 기존 경기 유지도 보존한다. 초기 기본값은 기존 **16인**이며, 배포 자체가 운영자를 대신해 64인으로 바꾸지는 않는다.

마이그레이션 `0030_survival_64_player_format.sql`은 설정 열과 64위/63봇/R4 허용 제약만 추가한다. 필수 migration 목록과 readiness 검사를 함께 확장했다. 0020/0029, 계정·경기·Attempt·LP/MMR·도전과제/AP ledger·활성화 epoch는 초기화하지 않는다.

## 공용 보상과 UI

도전과제 카탈로그는 기존 48종이며 모드 크기로 분리하지 않는다. 같은 계정의 달성·칭호·AP와 조건을 공유한다. 64인 순위도 기존 16단계 LP 보상표의 순위 비율로 매핑하며 동점은 점유한 최하위 순위 보상을 사용한다. 우승 +50, 최하위 -55인 기존 기준을 유지한다.

64인 플레이어 카드는 **닉네임과 아이콘만** 만든다. 순위 접두사, 티어 배지, 진행률·LP 라벨과 3개 메달 패널은 생략한다. 닉네임 색과 기존 탈락 배경색으로 내 플레이어·탈락 상태를 구분하고 카드 클릭의 프로필 보기를 유지한다. 화면에 들어가지 않으면 페이지를 나누며 카드 객체를 재사용한다. 전체 순위창도 64명 전체를 보존하고 32명씩 탐색할 수 있다. 16인 카드와 시상식의 결과 정보는 기존 형태를 유지한다.

로비·대기열·Debug 입력·봇 채우기 한도는 현재 운영방식을 읽는다. 기록창은 실제 R4와 맵 상세 진입을 지원한다. 진행 중 경기의 미확정 순위/LP 숨김, 완료 라운드만 공개하는 기록 정책과 native 복귀/100% 재입장 보호는 유지한다.

## 검증과 배포

검증 결과와 배포 SHA·워크플로·R2 다운로드 해시는 게시 및 실제 활성화가 끝난 뒤 이 문서에 추가한다. 실기기 UI/터치/64명 부하와 실제 Neon Compute 절전은 네이티브 컴파일·격리 SQL 테스트·서버 health 확인과 구분한다.

로컬 전체 회귀에서 rules 288개, 서버 612개(실제 PostgreSQL 환경 전용 1개는 별도 실행), native 280개가 통과했다. 실제 PostgreSQL 16의 HTTP 동시성·64번째 참가자 매칭·연결 복구 53개도 통과했다. build/typecheck 및 번역 670 UI 키/48종을 확인했다. 16인과 64인 모두 가상 6분 유휴 SQL이 0개이며 대기열 활성화·종료 후 재유휴를 검증했다.

SDK JSON 배열을 명시적으로 소유하는 순위표 구현과 R4 컷오프 비교를 별도 native 검증에 포함했다.

### beta.97 게시 확인 (2026-10-24 UTC)

PR [#33](https://github.com/ybaf100/Corum-Ranked/pull/33)의 최종 소스는 `ca6d1d9621c9288b984426c68a39c3af1259c501`이며, 패키지 빌드 기준 main 병합은 `50f008bceb244d5728d445b4cace0ddb7e207856`이다. 두 커밋의 소스 트리는 같다. 패키지 공개 전 서버 요구 버전을 활성화하지 않도록 병합 시 서버 배포를 명시적으로 보류했다.

- [main Validate](https://github.com/ybaf100/Corum-Ranked/actions/runs/40099824906): success.
- [main Release](https://github.com/ybaf100/Corum-Ranked/actions/runs/40099824922): Windows/iOS/Android32/Android64 빌드, 패키징, `publish-download / publish` 모두 success.
- [main Debug](https://github.com/ybaf100/Corum-Ranked/actions/runs/40099824914): native 280개, 네 플랫폼 Debug ON/OFF, 패키징, `publish-download / publish` 모두 success. 선택적 외부 이미지 팩은 게시하지 않았다.

공개 R2 descriptor와 `.geode` 전체를 다운로드해 descriptor의 `sizeBytes`/`sha256`, 내부 버전·채널, 네 플랫폼 바이너리 포함 여부를 검증했다. 기존 immutable 패키지는 덮어쓰지 않았다.

| 채널 | 바이트 | SHA-256 |
| --- | ---: | --- |
| Release | 8746458 | `da45e183e4675ff2a57b5d5f2990a7e99e5a26e44d46530afe9ef353617ece40` |
| Debug | 9330019 | `98586112cec7ec1ad355a0c917d3ce62548638d1c08dc6e7d809b1b0e1034dfb` |

Descriptor: [Release](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/release-v0.5.0-beta.97/release.json), [Debug](https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev/ranked/debug-v0.5.0-beta.97/debug.json).

운영 배포 전 읽기 전용 DB 기준(13:34:02 UTC)은 계정 3개, Solo 경기 52건/Attempt 150건, Survival 경기 89건/Attempt 12395건, 해금 144행/AP ledger 144행/총 AP 5120이었다. 이는 계정별 기존 48종 카탈로그 데이터이며 신규 도전과제를 추가한 것이 아니다. 도전과제 활성화 epoch는 `2026-10-24T04:59:47.102Z`다. 0020/0029 적용을 확인했으며 활성 Survival 경기와 대기열은 0이었다.

양 채널 공개 파일 검증 후 이 문서의 별도 `[deploy-lightsail]` 커밋으로 Singapore의 정상 Docker 배포·rollback 절차를 시작한다. 새 0030은 서버 startup migration으로 적용하고 기존 0029 초기화를 반복하지 않는다. 실제 배포 결과와 배포 후 읽기 전용 보존 검증은 아래에 이어서 기록한다.
