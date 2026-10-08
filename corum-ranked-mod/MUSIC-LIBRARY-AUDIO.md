# v0.5.0-beta.42 — Ranked 메뉴 Music Library 음악 설정

이 변경이 포함된 클라이언트를 설치한 뒤, 기존 Ranked 설정 스프레드시트의
`Ranked Config` 시트에서 다음 값을 입력한다. 새로운 열이나 음원 URL은 필요 없다.

| 키 | 입력값 |
| --- | --- |
| `rankedAudioEnabled` | `TRUE` |
| `rankedAudioMenuSongId` | GD Music Library에서 표시하는 **전체 Song ID** |
| `rankedAudioMenuStartSeconds` | 재생을 시작할 위치(초). 처음부터는 `0` |

ID는 게임에서 복사한 숫자를 그대로 사용한다. 앞자리 제거, 접두사 추가,
Newgrounds ID 변환을 하지 않는다. `10000001`은 테스트용 예시이며 추천 곡이 아니다.

기존 Apps Script 설정 API를 통해 서버가 새 설정을 읽으면 Ranked 메뉴 진입 시
부족한 곡을 기존 리소스 다운로드 창에서 받을 수 있다. 이미 받은 곡은 다시
다운로드하지 않는다. 다운로드 실패 시 같은 창에서 재시도할 수 있다.
곡을 다른 ID로 변경하면 새 곡만 다운로드 대상이 된다.

강등 경고/승급전 로비, 매칭·라운드 준비 대기, 승리·패배 결과의 기존 `SongId`
키도 같은 방식으로 Newgrounds 및 Music Library ID를 받는다. 맵의 게임플레이
음악이나 Jukebox 곡 선택을 이 설정으로 변경하지 않는다.

## 다운로드 경로와 검증 범위

- ID가 `10000000` 이상이면 GD `MusicDownloadManager::downloadSong`을 사용한다.
  GD의 custom-content URL을 먼저 준비하고, 같은 곡의 기존 native 요청이 있으면
  함께 기다린다. CDN 주소를 하드코딩하거나 MusicDownloadDelegate를 교체하지 않는다.
- 그보다 작은 ID는 기존 Newgrounds 메타데이터/음원 다운로드 경로를 유지한다.
- 진행 중인 native 파일을 준비 완료로 인정하거나 캐시에 복사하지 않는다.
  완료 후 파일 크기와 음원 헤더를 확인하고 mod의 `ranked-audio` 저장 폴더로
  가져온다. 취소/설정 변경은 Corum의 추적을 끝내며 GD나 다른 mod의 요청을 중단하지 않는다.
- 시작 위치, 반복 재생, fade 설정, 개인 볼륨은 기존 정책을 사용한다.
  서버/client exact version gate는 함께 `v0.5.0-beta.42`로 올린다.
  config/protocol 형식은 변경하지 않는다.

기본 API와 ID 분기 참고:
[GD 2.2081 MusicDownloadManager 바인딩](https://github.com/geode-sdk/bindings/blob/main/bindings/2.2081/GeometryDash.bro),
[native Library 다운로드 사용 예](https://github.com/RayDeeUx/TowerSecretEnding/blob/8788c522ba531738523d9796a5c1e60b99f37a14/src/AssetDownloader.cpp).

`RankedAudioDownloadNativeTest.mjs`는 실제 Corum 다운로드/캐시 코드를 컴파일해
Library/NG 혼합, native 요청 공유, 실패·재시도·타임아웃, 설정 변경 후 이전
콜백, 재진입 캐시를 검증한다. GD/HTTP는 boundary doubles이며 실제 iOS/CDN
다운로드 또는 기기 재생을 검증했다는 의미는 아니다. 재생 시작 위치와 fade는
별도의 `RankedAudioTransitionNativeTest.mjs`로 회귀 검증한다.
