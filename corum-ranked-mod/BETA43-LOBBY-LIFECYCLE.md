# beta.43 검증 기록

Authoritative base: `ybaf100/Corum-Ranked` remote main
`bee630f451c142bba0a9453baa43deee876f00f9` (beta.42).
변경 대상은 이 저장소뿐이며 에셋팩 입력, Geode/Jukebox source, 관전 protocol v2와
Debug recording format v2는 변경하지 않았다.

## 확인한 원인과 수정

- **항복 다음 라운드의 봇:** 서버 항복 경로는 양쪽 authoritative Attempt를 종료하지만
  Debug Bot driver의 정상 End callback을 거치지 않는다. driver가 이전 라운드의
  Attempt/progress를 보유해 다음 맵에 전송하고 tick의 오류 재시도에 갇힐 수 있었다.
  authoritative round/deathmatch context가 바뀌거나 Result에 진입할 때 정리한다.
  같은 context의 LAST_ATTEMPT_WINDOW/ROUND_SETTLING에 허용된 마지막 Attempt는 유지한다.
  domain Attempt ID는 round 범위여서 `B-1` 같은 이름이 반복될 수 있다. 회귀 검증은 ID의
  문자열 차이가 아니라 실제 R2 DB row 생성, 이전 row 종료와 새 progress를 확인한다.
- **결과창 ESC:** terminal result에서 일반 Close가 Ranked scene을 pop하는 경로를 타던
  것을 기존 Result Close 동작으로 연결한다. Placement result의 ACK가 아직 진행 중이면
  결과를 유지한다. 진행 중 경기의 ESC 제한과 다른 페이지의 Back 동작은 유지한다.
- **주크박스:** 추가 확인 carrier UI를 없애고 설치된 모드의 실제 CustomSongWidget callback을
  바로 활성화한다. callback이 생성한 native dialog만 추적하고 닫기/scene exit 때 자신의
  timer·priority를 해제한다. gameplay launch의 기존 modal gate보다 먼저 이 dialog를 닫는다.
  주크박스 dialog가 열렸을 때만 scene 우측 위에 기존 준비 phase 기준 countdown을 표시한다.
  다른 모드의 미리 열린 modal은 강제로 닫지 않는다. 미설치 시 버튼은 기존처럼 숨긴다.
- **도전과제:** claimable reward가 있는 card의 원래 index를 먼저 표시한다. 안정적인 순서를
  유지해 pagination과 Claim/Title callback이 원래 서버 card를 참조한다. 수령 가능 버튼은
  기존 Corum 빨간색 스타일을 사용한다.
- **LDM:** exact Saved GJGameLevel 객체에 남은 이전 `m_lowDetailModeToggled`가 다시 쓰였다.
  매 round context에서 OFF로 시작하고 그 round에서 사용자가 직접 고른 값을 적용한다.
  native metadata/download가 객체를 교체해도 현재 선택은 보존한다. author capability인
  `m_lowDetailMode`, 점수와 메타데이터는 변경하지 않는다.

## 기기 중복 접속과 종료

로비 admission에서 player row를 잠그고 살아 있는 다른 process의 session이면 거부한다.
clientInstanceId는 매 게임 프로세스의 128-bit random ID이고 저장하지 않는다.
같은 process의 재시도는 token을 교체한다. DB의 기존 environment_snapshot에 installedMods와
instance ID를 함께 저장하므로 새로운 DB migration은 필요하지 않다.

heartbeat는 기존 process scheduler에서 5초마다 보내므로 로비가 native gameplay/Pause에
가려져 있어도 유지된다. 정상 로비 Close는 정확한 token을 해제하고 queue entry를 지운다.
강제 종료·연결 단절은 마지막 heartbeat에서 20초 뒤 만료한다. 이미 만료한 session은
heartbeat로 부활하지 못하고 SessionGuard도 거부한다. admission/heartbeat/Close는 같은
player row에서 serialize하고 queue/session lock 순서도 맞춘다. 오래된 token의 Close나
클라이언트 callback이 새 session을 건드리지 못한다. 서버의 기존 장기 token TTL도 유지한다.

## Pause 실기기 결과

사용자는 beta.42의 Pause 수정이 해결되었다고 보고했다.
첨부 `Geode 2026-10-08 14.12.18.log`에서 R1 항복 복귀 후 R2 GO와
14:13:30의 `[RankedPause] opened`를 확인했다. 이번 패치는 그 Pause/native Quit 처리를
수정하지 않는다. 이 결과로 특정 iOS private guard의 writer를 역으로 확정하지 않는다.

## 검증

- server: 34 suites / 209 tests passed. 실제 HTTP/PostgreSQL-compatible DB 회귀 포함.
- rules: 8 suites / 114 tests passed.
- client source/native boundary suites: 117 tests passed.
- strict C++ executable suites: 9 passed; external pack policy tests: 5 passed.
- TypeScript typecheck와 diff whitespace 검증 통과.

실제 Corum lifecycle bodies를 C++로 추출해 heartbeat 만료·늦은 응답·정확한 token Close,
주크박스 callback/owned dialog/timer/priority와 Result ESC를 실행했다. Cocos/GD/HTTP 경계는
doubles이며 실제 Windows 또는 iPad의 native 동작을 검증했다고 주장하지 않는다.
새 UI·주크박스 기기 확인과 사용자 Windows 전체 모드 빌드는 아직 실행하지 않았다.

## 이번 버전 빌드

client manifest, 서버 exact gate, npm workspace/lockfile을 beta.43으로 함께 변경했다.
사용자 요청에 따라 beta.43만 GitHub native build/package를 건너뛴다. 자동 테스트는 유지하고
다음 버전부터 기본 CI native build를 재개한다. manual Release는 실행하지 않는다.

Windows 준비 사항, 빌드 명령과 산출물은 [WINDOWS-BUILD.md](WINDOWS-BUILD.md)를 따른다.
이미지 에셋팩은 변경되지 않아 새 패키지나 업로드가 필요하지 않다.
