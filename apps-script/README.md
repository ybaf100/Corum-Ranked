# Corum Integration API

기존 맵 스프레드시트에 바인딩해서 쓰는 Google Apps Script 웹 앱이다.

## 설치

1. 맵 데이터를 관리하는 Google Spreadsheet를 연다.
2. `확장 프로그램` → `Apps Script`를 누른다.
3. `Code.gs` 내용을 이 폴더의 `Code.gs`로 교체한다.
4. 프로젝트 설정에서 `appsscript.json` 표시를 켠 뒤 이 폴더의 파일과 맞춘다.
5. 맵 데이터가 첫 번째 시트가 아니라면 스크립트 속성 `MAPS_SHEET_NAME`에 탭 이름을 입력한다.
6. `setupCorumIntegration`을 한 번 실행하고 Sheets/Drive 권한을 승인한다. `CorumPlayers`, `Records`, `CorumPublicClears`, `CorumVerifierRecords`, `CSMP Tiers`, `ClearEvidence`, `Corum Client Versions` 탭과 맵 시트의 `최소 등록 가능 기록`, `대체 맵 코드`, `CSMP 티어 배정` 열이 준비된다. Drive에는 비공개 `Corum Clear Evidence` 폴더가 자동 생성된다.
7. `배포` → `새 배포` → `웹 앱`에서 다음처럼 설정한다.
   - 실행 사용자: 나
   - 액세스 권한: 모든 사용자
8. `/exec`로 끝나는 배포 URL을 복사한다.

## 플레이어 자동 등록

개인 토큰은 사용하지 않는다. 처음 기록을 제출하면 모드가 전송한 현재
Geometry Dash 계정 ID와 닉네임이 `CorumPlayers`에 자동 등록된다.

특정 계정의 기록 등록을 차단하려면 `CorumPlayers` 탭에서 해당 행의
`활성` 값을 `FALSE`로 바꾼다. 다시 허용하려면 `TRUE`로 되돌린다.
이전 버전에서 생성된 `토큰 해시` 열은 남아 있어도 무시되므로 삭제할 필요가 없다.

## 웹사이트에서 기록과 점수 표시

웹사이트와 C Integration 모드는 이 웹 앱의 `/exec` URL을 사용해 맵별 기록과
플레이어 점수를 불러온다. 배포 주소는 사이트의
`public/corum-endpoint.json`에 한 번만 기록한다.

`CorumPlayers`, `Records`, `ClearEvidence`는 계정 상태·중복 검사, 점수 집계,
End Screen 증거 연결에 쓰는 비공개 탭이다. 세 탭은 웹에 게시할 필요가 없다. 이전 버전이 만든
`CorumClears` 탭이 남아 있어도 기록 조회와 점수 집계에서 함께 읽는다.

API 2.23의 `Records`에는 `맵 코드` 바로 오른쪽에 `맵 제목` 열이 있으며 새 기록과
갱신 기록 모두 현재 관리용 맵 제목을 저장한다. `setupCorumIntegration()`을 실행하면
기존 기록의 맵 제목도 `sheet1` 기준으로 한 번 채워진다.

`Records`와 `ClearEvidence`의 `사용 모드 목록`은 클라이언트가 게임 시작 시 한 번
수집한 로드된 Geode 모드 ID와 버전(`mod.id@version`)을 저장한다. 이 열은 웹 공개용
`CorumPublicClears`와 공개 API 응답에는 복사하지 않으며 기록 판정에도 사용하지 않는다.

## 지원 버전과 업데이트 주소

`setupCorumIntegration()`이 만드는 `Corum Client Versions` 탭이 클라이언트 지원
정책의 단일 기준이다.

| 플랫폼 | 최소 지원 버전 | 최신 버전 | 업데이트 URL | 버전 검사 활성 |
| --- | --- | --- | --- | --- |
| Windows | `v1.0.0` | `v1.0.2` | 공식 GitHub Releases 주소 | `TRUE` |
| Android | `v1.0.0` | `v1.0.2` | 공식 GitHub Releases 주소 | `TRUE` |
| iOS | `v1.0.2` | `v1.0.2` | 공식 GitHub Releases 주소 | `TRUE` |

`최소 지원 버전`보다 낮은 모드는 맵 목록과 레벨 정보를 계속 조회할 수 있지만,
`record`와 `batchRecords` 제출은 `CLIENT_OUTDATED`로 거절된다. 모드도 같은 정책을
게임 실행 시 한 번 받아 `C Integration is outdated!` 경고를 표시하고 종이비행기
버튼을 눌렀을 때 업데이트를 요구한다.

새 버전을 배포할 때 이 탭의 `최신 버전`과 `업데이트 URL`을 먼저 갱신한다. 구버전
제출까지 막으려는 시점에만 `최소 지원 버전`을 올린다. `버전 검사 활성`을 `FALSE`로
바꾸면 해당 플랫폼의 서버 차단을 임시로 해제할 수 있다. 버전은 반드시
`v1.2.3` 형식으로 입력한다. 업데이트 URL은 안전을 위해
`https://github.com/ybaf100/Corum-integration/releases` 아래 주소만 클라이언트에
전달한다.

v1.0.0 통합 출시용 `Code.gs`에서 `setupCorumIntegration()`을 실행하면, 이전 코드가
만든 Android 기본값 `v0.2.40`/`v0.2.40` 행은 `v1.0.0`/`v1.0.0`으로 자동
이행된다. 운영자가 그 행을 다른 값으로 직접 바꿨다면 덮어쓰지 않는다.

v1.0.2 배포용 코드에서는 기본 Windows·Android 정책의 최소 지원 버전을
`v1.0.0`으로 유지한 채 최신 버전만 `v1.0.2`로 이행한다. 중간 작업본의
`v1.0.1` 정책도 같은 기본 정책으로 확인되면 함께 이행한다. iOS는 처음
지원되는 `v1.0.2`를 최소·최신 버전으로 사용한다. 운영자가 직접 수정한
정책 행은 덮어쓰지 않는다.

## 자동 End Screen 증거

API 2.23은 `POST action=evidence`를 받아 원본 PNG를 Drive의 비공개
`Corum Clear Evidence` 폴더에 저장하고 `ClearEvidence` 탭에 메타데이터를 남긴다.
클라이언트가 보낸 폭·높이는 신뢰하지 않고 PNG IHDR에서 서버가 직접 다시 읽는다.
이미지는 리사이즈하거나 재인코딩하지 않고 업로드된 PNG 바이트 그대로 보관한다.

100% 기록이 이미 있으면 증거 업로드 시 해당 `Records` 행과 연결한다. 반대로 이미지가
먼저 도착하면 이후 수동 기록 등록 시 같은 GD 계정·같은 Corum 맵의 최신 증거를 찾아
연결한다. 따라서 이미지 업로드와 기록 등록의 도착 순서에 의존하지 않는다.

`Records`와 `CorumPublicClears`에는 `엔드스크린 증거 ID`, `엔드스크린 파일 URL`이
같이 저장되며 기존 `증거` 열에도 같은 Drive URL을 기록한다. 2.21 이하에서
`ClearEvidence`에만 남은 기존 항목은 새 `Code.gs`로 교체한 뒤
`setupCorumIntegration()`을 한 번 실행하면 계정+대표 맵 기준 최신 증거로 다시 연결된다.
End Screen 자동 업로드 자체는 `Records`를 만들지 않는다. 기록 등록은 기존처럼 게임에서
사용자가 `Submit` 또는 `Submit All`을 눌렀을 때만 수행한다.

## 최소 등록 가능 기록

맵 관리 시트의 `최소 등록 가능 기록` 열에 `65` 또는 `65%`처럼 `1~100` 값을 입력한다. 값이 비어 있거나 숫자로 해석할 수 없거나 범위를 벗어나면 API는 해당 맵의 최소 기록을 `100%`로 처리한다.

## 대체 맵 코드

같은 Corum 맵으로 인정할 다른 Geometry Dash 레벨이 있으면 해당 행의
`대체 맵 코드`에 레벨 ID를 입력한다. 비어 있거나 숫자가 아니거나 대표
`맵 코드`와 같은 값이면 대체 맵이 없는 것으로 처리한다.

대표 코드와 대체 코드 중 어느 쪽으로 조회하거나 제출해도 API는 대표 맵으로
정규화한다. 새 Records 행에는 대표 코드만 저장되며, 과거 행에 두 코드가
섞여 있더라도 계정과 대표 맵별 최고 기록 하나만 기록 목록과 점수에 반영된다.

## 최고 기록 갱신 시 점수 재확정

점수는 플레이어가 해당 맵의 기록을 처음 제출하거나 서버에 저장된 최고
퍼센트보다 높은 기록을 제출한 순간에 확정된다. `Records`에는 점수 반영 기록,
점수 반영 순위, 점수 반영 최소 기록, 확정 점수, 점수 공식 버전, 점수 확정
시각이 함께 저장된다.

맵 순위나 최소 등록 기록만 바뀌어도 이미 확정된 점수는 다시 계산하지 않는다.
같거나 낮은 기록을 다시 보내도 유지된다. 더 높은 기록을 보내면 그 제출 시점의
맵 순위, 최소 등록 기록, 새 퍼센트로 점수를 다시 계산해 기존 확정 점수를
교체한다.

API 2.5 이상으로 업데이트한 뒤 `setupCorumIntegration`을 실행하면 이전 기록에도 점수
스냅샷 열이 추가된다. 과거 순위 데이터는 기존 시트에 없으므로 이전 기록은
마이그레이션 실행 시점의 맵 순위와 현재 저장된 기록을 기준으로 한 번 확정된다.
API 2.4의 `최초 등록 ...` 열 이름은 자동으로 새 이름으로 바뀌며 값은 유지된다.

## 기록 등록과 검증

플레이어는 저장된 최고 기록이 맵의 최소 기록 이상일 때 게임의 맵 정보 화면 왼쪽 버튼으로 직접 등록한다. 같은 계정·맵의 기록이 더 높아지면 같은 버튼으로 다시 보내 기존 행을 갱신할 수 있다. 새 기록과 갱신 기록은 기본적으로 `unverified` 상태다. 운영자가 영상 등 증거를 확인한 뒤 다음 함수를 실행하면 웹사이트에도 상태와 증거 링크가 반영된다.

```js
setClearStatus("레코드-ID", "verified", "https://증거-주소");
```

상태는 `unverified`, `verified`, `rejected` 중 하나다. 증거 링크를 비우려면 세 번째 인수에 빈 문자열을 넣는다.

## API

- `GET ?action=health`
- `GET ?action=map&levelId=97883413`
- `GET ?action=map&levelId=97883413&gdAccountId=12345678`
- `GET ?action=list`
- `GET ?action=clears&levelId=97883413`
- `GET ?action=scores&limit=500`
- `GET ?action=playerRecords&gdAccountId=12345678`: 특정 계정의 현재 확정 점수와 맵별 기록만 조회
- `POST action=record`: 맵 하나의 최고 기록 등록·갱신
- `POST action=batchRecords`: 최대 200개 맵의 기록을 요청 하나로 일괄 등록·갱신
- `POST action=evidence`: 완료 End Screen 원본 PNG 증거 업로드

`batchRecords`는 계정 정보와 `records` 배열을 받으며, 하나의 스크립트 잠금과
시트 일괄 쓰기로 처리한다. 일부 기록이 잘못되어도 나머지는 계속 처리하고
맵별 성공·실패를 `results` 배열로 반환한다.

`scores`는 `GD 계정 ID + 대표 맵 코드`별 최고 미반려 기록 하나와 최근 점수 변경
제출에서 확정된 점수를 사용해 플레이어 종합 순위를 반환한다. 각 플레이어 응답에는
프로필 화면에서 사용하는 `accountId`와 맵별 `records` 배열도 포함된다.

`playerRecords`는 같은 집계 규칙을 사용하되 지정 계정을 전체 순위 결과에서 찾는
방식이 아니라 집계 전에 계정 ID로 제한한다. 따라서 플레이어가 500명을 넘어도 게임의
`Submit All` 검토창이 현재 계정의 기존 기록을 놓치지 않고 불필요한 전체 점수표도 받지 않는다.

`list`와 `map` 응답은 대표 `levelId`와 유효한 경우 `alternateLevelId`를 함께
반환한다. `list`에는 현재 `ClearEvidence` 탭의 고유 ID를 `evidenceGeneration`으로,
플랫폼별 지원 버전 정책을 `clientPolicy`로 함께 반환한다. 탭을 삭제하고
`setupCorumIntegration()`으로 다시 만들면 이 값이 자동으로
바뀌어 클라이언트의 과거 완료 캡처 표시가 새 서버 상태를 가리지 않는다.
`map`, `clears`, `record`, `batchRecords`는 두 ID를 모두 허용한다.

`map`에 `gdAccountId`를 함께 보내면 해당 계정의 기존 기록과 현재 확정 점수를
`playerRecord`로 반환한다.

Apps Script Content Service의 응답 상태는 JSON의 `ok` 필드로 판별한다.
