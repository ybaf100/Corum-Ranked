# Survival beta.88 후속 수정

기준 main: `a6fb65bd1124980b087d9142b94654e7d781817e` (beta.87).
기존 beta.85 통합 상태 머신, beta.86 폰트/패키지 정책, beta.87 SDK/바인딩/Codegen 고정은 유지한다.

## 원인과 변경

- **항복 후 수동 종료 필요**: `queueHeldEntryExit()`는 `autoExitRequested`를 먼저 고정한 뒤 `onQuit()`을 한 번만 호출했다. Pause가 그 호출을 거절해도 플래그가 남아, ACK 처리 쪽의 안전한 종료도 재시도할 수 없었다. Survival 진입 취소도 기존 `queueSurvivalGameplayReturn()`의 재시도 가능한 Deferred Quit으로 통일했다. 프로세스 소유 preparation heartbeat가 Survival tick/종료 요청을 진행하므로 Pause된 PlayLayer의 스케줄러를 기다리지 않는다. 서버 탈락/라운드 종료 증거, WeakRef, 정확한 PlayLayer/Scene 소유권, completion 및 Pause 정리 게이트는 유지한다.
- **썸네일 준비 경쟁**: 라운드 reveal마다 후보를 지우고 random 요청을 다시 시작하던 정책을 변경했다. 첫 준비에서 최대 10개 후보를 확보하고 같은 경기의 다음 라운드는 그 캐시의 순서/최대 7개 부분집합을 바꾼다. 다음 라운드의 random API 호출은 없다. 공식 선정 맵의 small/medium 요청과 reveal/Scene 세대 검증은 별개로 유지한다. 2개 다운로드 슬롯, 40 HTTP/활성화, 20 random/경기, RAM 12개/16MiB 및 디스크 32개/32MiB 제한을 유지한다.
- **선정 맵 정보**: intro의 맵 ID를 숨기고 설정 명칭을 `레벨 썸네일 모드`로 맞췄다. label-free 얼굴로 바뀐 실제 캔버스 중심에 native Featured/Epic/Legendary/Mythic 장식 루트를 정렬한다. FINAL/Death Match의 두 줄 타이머는 줄바꿈 폭을 해제한 채 전체 문자열을 안전 영역에 맞춘다. 난이도 아래에 실제 GD SongInfo의 첫 곡 이름을 `Song - 제목`으로 표시한다. 작은 화면 및 숫자 모드에서 토글과 겹치지 않도록 별도 행을 확보한다. 실제 GD 별 스프라이트와 얼굴 중복 단어 제거는 유지한다.
- **Demon GDDL**: 네이티브 GD appearance 응답에는 GDDL tier가 없고 패널에 이를 보완하는 경로가 없었다. 기존 서버의 exact-ID GDDL 규칙/색을 사용해 표시 전용 캐시를 보강한다. 한 요청만 공유하고 5분 재시도, 4초 timeout, 응답 크기/숫자 범위/캐시 대상 검사를 적용한다. GD search delegate나 공식 맵 인증은 건드리지 않는다. native remember가 보강한 tier를 다시 지우지 않게 한다.
- **결과/시상식**: 개인 결과나 시상식 진입 시 실제 서버 settlement로 기존 LP 애니메이션을 자동 실행한다. 아직 입력이 불완전하면 완료 플래그를 기록하지 않아 다음 갱신에서 재시도한다. 경기별 중복 방지 저장은 유지한다. 3위 → 2위 → 1위의 단상 상승, 아이콘 낙하, 티어/점수 stamp를 별도 단계로 표현하고 연출 완료 전에 아이콘 입력을 허용하지 않는다. 정산 후 LP 옆에 증감량을 붙이며, 늦은 정산 응답은 연출 시간을 되돌리지 않는다. 1위는 기존 GOLD Matched ambient를 사용한다. 결과/시상식의 LP 수동 열기 및 현황판 버튼은 제거한다. 탈락 선택의 보드 진입과 공식 결과 LP 표시만 유지하며, 전체 카드 뒤의 전역 LP footer는 복원하지 않는다.
- **관전/100% 재입장**: 관전의 명시적 종료는 개인 결과 선택과 로컬 FREE PLAY 반환 요청으로 처리한다. Pause와 준비 패널에 관전 나가기를 제공한다. 관전 재입장/수동 시작과 ACK된 100% 맵의 재입장은 유효한 세션/준비 context/맵/남은 시간을 검증한 로컬 전용 플레이로 시작한다. 기존 `canPlay`, `beginAttempt`, `progress`, `endAttempt`의 비경쟁 차단을 유지해 공식 순위·LP·클리어 기록을 바꾸지 않는다.
- **Debug 즉시 종료**: FORCE CLEAR 옆에 END ROUND를 추가했다. 자체 확인창, 현재 권한, 진행 중인 경기/라운드/맵 세대, 요청 중 중복 입력 차단을 사용한다. 마지막 공식 attempt end를 먼저 보내고 전용 `/api/ranked/survival/debug/matches/:id/end-round`의 ACK를 기다린다. 서버 `DEBUG_END_ROUND`는 기존 `finishRound`로 현재 접수 기록만 마감한다. 원래 180초 deadline을 보존하고 `finishedAt`으로 조기 종료하므로 DB 시간 제약도 유지한다. 온라인 일반 event DTO에는 추가하지 않았고 Release 클라이언트에는 UI/호출 경로가 없다.
- **HUD**: 점수 차 색상은 현재 공식 순위/통과 여부를 따른다. 탈락권/탈락 확정은 빨강, 특별 색 없는 통과 순위는 초록이다. FINAL/TIEBREAK progress는 금색 선두 표시만 남기고 cutoff/개인 색 선을 생략한다. 인게임 후광과 플로팅 현황판 버튼은 계속 제거된 상태다.
- **리더보드**: 실제 현재 row의 identity로 기존 커스텀 Player Info를 연다. 스크롤 제스처는 row 클릭을 취소한다. 인증된 표시 전용 `/api/ranked/leaderboard/player-info/:accountId`는 등록된 정확한 GD 계정의 기존 포럼 캐시만 읽는다. 봇 조회/가짜 계정/외부 refresh는 생성하지 않는다. Solo/Survival 표시, GD delegate 공유 및 팝업 수명 검사는 유지한다.
- **Credits**: 버튼은 **설정 탭 내부**에 있다. `기획자 · hwanhee1`과 현재 config에 실제 연결된 BGM의 GD artist/song metadata를 `작곡가 - 노래명`으로 표시한다. 동일 곡의 사용처를 묶고 없는 메타데이터는 대기/불러오기 실패로 명시한다. 최대 16곡/곡당 2회 조회이며 글로벌 GD delegate를 점유하지 않는다. 새 한글 1글리프만 활성 폰트에 추가하고 예약 Regular ASCII atlas를 유지한다.

## 재현 및 검증 경계

`SurvivalPauseNativeTest.mjs`의 production held-exit/Deferred Quit 재현은 beta.87 `PlayLayer.cpp`에서 Pause 거절 후 고정 플래그 assertion이 실패하고 수정 코드에서 통과한다. 실행 파일 경계의 Cocos/네트워크 더블이며 실기기 렌더러나 GD 실제 teardown 확인을 대신하지 않는다.

신규 테스트는 실제 provider의 후속 라운드 random 요청 0회, cached preview, GDDL exact-ID/지연·캐시 제거, 판넬 레이아웃, 시상식 단계/입력, 자동 LP, Credits 실제 metadata, 리더보드 tap/drag, 관전 종료/로컬 재입장 격리, Debug 종료의 HTTP 인증·멱등성·기록 보존 및 FINAL/TIEBREAK를 검사한다. 기존 서버/네이티브 회귀 및 Release/Debug 플랫폼 패키지 검증도 실행한다. 각 실행 결과와 게시 SHA/URL은 최종 제출 보고서에 별도로 기록한다.

실기기 추가 확인: iPadOS Pause 항복 ACK 직후 자동 복귀/재입장 반복, 관전 종료와 무료 플레이의 GD 기록 비변경, iPad 장식 위치·FINAL 타이머·긴 한글 노래명, 시상식 rise/drop/stamp 및 Gold glow, 스크롤 이후 리더보드/설정 Credits 터치, 느린 GDDL/썸네일 응답. CI 통과만으로 이 항목들을 실기기 검증 완료로 표시하지 않는다.

## 배포

beta.88 Release/Debug는 기존 보호된 CI로 새 immutable R2 경로에 게시한다. 양쪽 descriptor/패키지 존재·크기를 확인하기 전 Railway 요구 버전은 beta.87을 유지한다. 이전 beta.87 파일, Neon 데이터/스키마/접속 설정 및 Railway 환경 변수는 변경하지 않는다.
