# Survival beta64: 맵 검색, 대기 연출, R2 업데이트

기준 main: `2d8c29de27644b83a69c6632c7a444170f59c513`. 클라이언트와 서버의 버전을 `v0.5.0-beta.64`로 함께 올린다. 기존 M3/M4 경기 엔진, 준비 제한, 실제 LP 정산 및 Solo Ranked를 유지한다.

## 맵 선택

서버의 `GdSurvivalMapProvider`가 GD API를 직접 조회한다. 인기순으로 오래된 맵을 찾는 대신 최신 Rated 검색(`type=4`, `star=1`)으로 난이도별 후보 풀을 만든다. 일반 난이도와 Demon 세부 난이도는 별도 검색한다.

- 풀은 10분 동안 유지하며 검색 조건별 최대 256개 후보를 저장한다. 최근 256페이지 범위에서 방문하지 않은 페이지를 무작위로 추가한다. 동시에 같은 풀을 처음 요청하면 검색을 공유한다.
- 경기에서 이미 사용한 ID를 제외하고 정확한 라운드 난이도, Rated, ID 40,000,000 이상, 비 Auto·비 Platformer 조건을 적용한다. 후보 부족으로 조건을 완화하거나 존재하지 않는 ID를 만들지 않는다.
- 선택한 후보를 매번 실제 다운로드한다. 다운로드 응답의 현재 메타데이터와 레벨 버전, 압축 해제된 실제 오브젝트 데이터 및 SHA-256을 검증한 뒤 서버가 맵을 확정한다. 검색 당시 별점이 바뀌었어도 현재 메타데이터가 해당 라운드 조건을 만족하면 사용할 수 있다.
- 잘못된 맵 데이터와 메타데이터는 후보에서 제거하고 2분간 제외한다. API 장애는 맵 자체의 영구 실패로 처리하지 않는다. 한 선택 시도는 최대 30초·12개 다운로드이며, 실패 시 기존 DB 임대·재시도 구조를 사용한다. 준비·플레이 타이머를 임의로 시작하지 않는다.
- `mapSelection.errorMessage`를 읽기 응답에 추가한다. 클라이언트는 새로운 오류가 발생했을 때 `[SurvivalMap]` 로그에 경기·라운드·generation·오류 코드·실패 이유를 남긴다. 인증키나 세션 토큰은 기록하지 않는다.

첨부 Geode 로그에는 시작 및 로비 음악 기록만 있어 운영 서버의 맵 실패 원인을 특정할 수 없었다. 작업 환경에서 최근 Rated 검색과 실제 GD 다운로드를 읽기 전용으로 확인했으며 5성·6성 최신 맵의 압축/메타데이터/오브젝트 데이터는 정상 응답이었다. 이는 운영 서버의 GD 접속이나 실기기 플레이를 검증한 결과가 아니다.

## 클라이언트 연출

- Survival 일반 라운드 대기는 Solo 일반 라운드 대기 음악(`Round12`)을 사용한다. 결승·추가 Death Match는 Solo Round 3 대기 음악을 사용한다. 실제 맵 진입 시 기존 PlayLayer가 대기 음악을 정리한다.
- 사용자 탈락 후에는 결과 음악을 사용한다. 최종 정산 이후에는 실제 LP가 증가하면 승리 음악, 같거나 감소하면 패배 음악을 사용한다.
- 마지막 완료 라운드 요약을 다음 라운드 응답에도 유지한다. 짧은 `ROUND_RESULT`를 놓친 클라이언트도 탈락한 플레이어의 배경을 붉게 바꾸고 아이콘을 날리는 연출을 한 번 재생한다. 1.05초 동안 다음 맵 자동 진입과 결과 모션을 대기시키되 서버의 180초 경기 규칙과 준비 시각은 바꾸지 않는다. 탈락한 사용자의 경기 종료 규칙도 그대로다.
- 모드 선택 화면의 랭크 카드는 마우스를 올리는 동안 확대를 유지하고 벗어나면 복원된다. 테두리 빛은 한 바퀴에 0.66초, 잔광까지 0.76초로 조금 느려진다. 선택한 채널 내부의 랭크 카드에는 hover/press 효과를 적용하지 않는다.
- 카드 교차 중에는 매칭 패널의 바깥 카드만 표시한다. 매칭 버튼·리더보드 버튼의 배경과 테두리를 포함한 모든 내부 내용은 이동 완료 후 0.18초간 함께 페이드인한다. 뒤로 가기는 내부 페이드아웃 이후 역방향 이동을 시작한다. GD의 자체 opacity로 그리는 `CCLayerColor` 테두리는 별도로 fade 처리한다.

## R2 최초 설정

Ranked의 Debug/Release 패키지 게시를 GitHub Releases에서 새 R2 버킷으로 전환한다. GitHub Actions 아티팩트는 계속 생성한다. 기존 소스 전달용 `corum-ranked-handoff` Worker와 다른 프로젝트의 Integration 배포는 변경하지 않는다.

1. Cloudflare **R2 Object Storage → Create bucket**에서 `corum-ranked-download`를 생성한다.
2. 버킷의 **Settings → Public Development URL → Enable**을 실행하고 표시된 `https://pub-….r2.dev` 주소를 복사한다. 사용자 지정 다운로드 도메인도 가능하다. 주소는 HTTPS origin만 입력하며 파일 경로와 query를 붙이지 않는다. S3 API Endpoint는 다운로드 주소가 아니다.
3. R2 첫 화면의 **Manage R2 API Tokens → Create Account API token**(이전 UI에서는 **Create API token**)을 연다. 이름은 `corum-ranked-download-actions`, 권한은 **Object Read & Write**, 적용 범위는 **특정 버킷 `corum-ranked-download`만** 선택한다. TTL은 만료 없음, Client IP 제한은 비워둔다. 생성 직후 표시되는 **Access Key ID**와 **Secret Access Key**를 GitHub Secrets에 등록한다. 별도의 **API Token** 값은 이 업로더에 사용하지 않는다. Secret Access Key는 생성 직후에만 표시되므로 페이지를 닫기 전에 저장한다. Account ID는 S3 주소 `https://계정ID.r2.cloudflarestorage.com`의 계정ID 부분이다. 키를 채팅이나 저장소 파일에 넣지 않는다.
4. GitHub 저장소 **Settings → Secrets and variables → Actions → Secrets**에 다음을 등록한다.

| Secret | 값 |
|---|---|
| `RANKED_R2_ACCESS_KEY_ID` | R2 Access Key ID |
| `RANKED_R2_SECRET_ACCESS_KEY` | R2 Secret Access Key |

5. 같은 화면의 **Variables**에 다음을 등록한다.

| Variable | 값 |
|---|---|
| `RANKED_R2_ACCOUNT_ID` | Cloudflare Account ID |
| `RANKED_R2_BUCKET` | `corum-ranked-download` |
| `RANKED_DOWNLOAD_BASE_URL` | 실제 공개 다운로드 origin |
| `RANKED_PUBLISH_R2` | `true` |

6. main push 후 **Build Corum Ranked Debug**, **Build Corum Ranked Mod**가 각각 해당 채널 패키지를 게시한다. 설정이 push보다 늦었으면 main에서 해당 워크플로를 수동 실행한다. PR과 다른 브랜치에서는 R2 게시를 실행하지 않는다. 플래그가 없거나 `false`면 자동 R2 게시가 꺼진다.
7. 최초 패키지와 descriptor 업로드 완료 후 Render 서버 **Environment**에 `RANKED_DOWNLOAD_BASE_URL`을 같은 주소로 등록하고 새 서버 코드를 배포한다. 서버는 공개 URL로 읽기만 하므로 R2 write 인증키를 넣지 않는다.
8. beta63까지의 클라이언트는 GitHub URL이 고정되어 있으므로 **beta64를 처음 한 번 수동 설치**한다. main Debug 워크플로 아티팩트 또는 새 R2의 Debug 패키지를 사용한다. 이후 업데이트는 서버가 보내는 R2 URL을 따른다.

공개 객체 경로:

```text
ranked/debug-v0.5.0-beta.64/Corum-Ranked-v0.5.0-beta.64-debug.geode
ranked/debug-v0.5.0-beta.64/debug.json
ranked/release-v0.5.0-beta.64/Corum-Ranked-v0.5.0-beta.64-release.geode
ranked/release-v0.5.0-beta.64/release.json
```

실제 공개 origin을 위 경로 앞에 붙이면 다운로드할 수 있다. Ranked 로그인은 요구하지 않는다. 최신 파일명으로 덮어쓰지 않고 채널·버전별 고정 경로를 사용한다.

`publish-mod-r2.py`는 패키지 ID·버전·채널 marker·4개 플랫폼 파일·hash·size를 검사한다. SigV4 S3 요청으로 패키지를 먼저 업로드하고 descriptor를 마지막에 게시한다. 조건부 업로드로 같은 경로에 다른 바이트를 덮어쓰지 않는다. 중단된 게시를 같은 파일로 재시도할 수 있지만 이미 게시한 버전을 다른 빌드로 교체하려면 새 beta 버전을 사용해야 한다.

서버는 정확히 현재 요구 버전의 채널별 descriptor만 읽는다. 다른 채널이나 다른 R2 패키지 경로를 가리키는 자동 descriptor는 거절한다. 기존 `RANKED_MOD_UPDATES_JSON` 명시적 pin API는 유지하며 URL이 없는 pin에는 설정된 R2 origin으로 정확한 경로를 추가한다. R2 origin이 없으면 자동 GitHub Releases 조회를 실행하지 않는다. 클라이언트의 hash·size·채널 및 설치 전 검사는 그대로 유지한다.

## 검증·운영 범위

기존 맵 제공자, 업데이트, 준비/경기 규칙 및 UI fixture에 후보 풀·메타데이터 최신화·완료 라운드 요약·R2 URL·조건부 게시·테두리 fade 관련 회귀 검사를 추가/갱신했다. 사용자 요청에 따라 이번 작업에서 로컬 테스트·빌드, Android 패키지 검사, 실기기 검사 또는 새 GitHub Actions 결과 확인은 실행하지 않았다. 코드 diff를 검토한 뒤 main에 push한다.

새 DB 마이그레이션과 Apps Script setup/재배포는 필요 없다. 운영 DB, Apps Script, R2 계정·버킷 및 서버 환경은 이 코드 작업에서 직접 변경하지 않았다. M5의 공개 다인원 매칭과 온라인 세션 연동은 기존 후속 범위로 남아 있다.

## Render 서버 빌드 핫픽스

`1daedb1878019cf36e0d9800361e4a50716477ae`의 서버 빌드는 `exactOptionalPropertyTypes`에서 `rankedDownloadBaseUrl?: string`에 명시적 `undefined`를 반환해 TS2375로 실패했다. R2 주소를 한 번 정규화한 뒤, 설정되지 않은 경우 반환 객체에서 해당 속성을 생략하도록 수정했다. 주소가 설정된 경우의 동작과 beta64 버전은 유지한다. 이 핫픽스는 서버 설정·회귀 검사·문서만 변경하며 기존 R2 클라이언트 패키지를 다시 게시하지 않는다.

이후 사용자 요청에 따라 로컬의 빠른 validate를 직접 실행했다.

- npm 전체 workspace 타입검사: 통과. rules/server 소스와 테스트의 strict TypeScript 검사를 포함한다.
- 서버 환경·업데이트 descriptor·맵 제공자·버전 진입 검사: 4파일, 70개 통과.
- Survival 경기 규칙 검사: 52개 통과.
- 로비 이동/테두리 fade 및 클라이언트 업데이트 native fixture: 9개 통과.
- R2 게시 fixture: 9개 통과.

위 결과는 로컬 validate 결과다. 전체 Geode 플랫폼 빌드·실기기 검사·운영 R2 업로드·새 GitHub Actions 결과를 확인한 것으로 보고하지 않는다.

## Render의 GD HTTP 403 진단 패치

후속 Geode 로그는 `getGJLevels21.php HTTP 403`을 기록했고, Render 로그는 서비스가 정상 시작한 뒤 같은 경기의 `API_ERROR`를 약 2초마다 반복했다. 개발 환경에서 같은 GD URL·본문·헤더는 HTTP 200으로 레벨 목록을 반환했고, 운영 Ranked의 공개 설정 API도 beta64로 정상 응답했다. Render에서 GD로 보내는 요청이 거절되는 것은 확인했지만 발신 IP·TLS/요청 특성·보안 challenge 중 어느 조건인지는 이 기록만으로 확정할 수 없었다.

서버는 실패 응답의 처음 4 KiB만 읽고 아래 항목만 기록한다. 원문 HTML, Cookie, Authorization 및 세션 토큰은 저장하지 않는다.

- GD endpoint, HTTP status, Server, Content-Type
- `cf-ray`, `cf-mitigated`, `Retry-After`
- 최대 160자의 HTML title와 challenge/block/HTML/text 분류

Render의 `Survival map selection …` 경고에 기존 오류 코드뿐 아니라 실패 이유와 `http={…}`가 포함된다. 정보는 `sur_matches.map_error`에도 저장되며, 읽기 응답의 `mapSelection.httpDiagnostics`와 `errorMessage`로 조회할 수 있다. 기존 beta64 클라이언트는 상세 `errorMessage`를 `[SurvivalMap]` 로그에 남기므로 클라이언트 재설치는 필요 없다. Shell을 사용할 수 없어도 다음 실패 시 Render 경고 한 줄로 추가 진단 정보를 얻는다.

403·429는 다른 난이도/후보로 즉시 반복하지 않고 기본 30초 기다린다. 유효한 `Retry-After`는 2초~120초 범위에서 반영한다. 한 서버 프로세스의 Survival 제공자가 대기 상태를 공유하고 DB에도 경기별 재시도 시각을 저장하므로 여러 경기·서비스 복구가 같은 차단 경로를 반복 호출하는 것을 줄인다. 일반적인 검색 실패의 기존 2초 재시도는 유지한다. 맵 조건, GD endpoint/요청 헤더, 경기 규칙, 시작된 라운드 타이머, LP 정산 및 Solo 기능은 바꾸지 않는다.

전체 workspace 소스/테스트 타입검사와 맵 제공자·경기 DB 통합 검사 39개를 통과했다. 검사는 오류 응답 크기 제한·민감 헤더 제외·403/429 대기·대기 후 회복·DB 재시도 복구 및 대기 중 타이머 미시작을 포함한다. 이 패치는 차단 원인을 드러내고 과도한 재요청을 줄인다. 운영 Render→GD의 접근 거절이 해소된 것으로 보고하지 않으며, 배포 후 실제 `http={…}` 로그로 다음 조치를 판단해야 한다. 클라이언트 패키지 재게시·새 마이그레이션·Apps Script 변경은 없다.

## Cloudflare 텍스트 403 후속 진단

2026-10-12 08:39:57 UTC의 Render 경고는 `Server=cloudflare`, `Content-Type=text/plain`, HTTP 403, `cf-ray=…-SIN`을 확인했다. HTML title과 `cf-mitigated`는 없었다. GD 검색 응답을 읽기 전에 거절되므로 라운드 난이도 필터나 후보 부족이 이 오류의 원인은 아니다. 단, 이 헤더만으로 발신 IP 차단, 요청 특성 검사 또는 다른 Cloudflare 정책 중 어느 것인지 확정하지 않는다.

이 응답에서는 이전 진단 코드가 텍스트 본문을 버렸다. 실패 응답의 4 KiB 읽기 제한을 유지하면서 Cloudflare 응답의 `error code: 1xxx`를 `edgeErrorCode`로 추출하고, HTML·challenge가 아닌 Cloudflare 텍스트 응답에 한해 최대 160자의 `textSummary`를 보관한다. Cloudflare 이름이 본문에 없어도 `Server` 헤더로 확인한다. Cookie·Authorization 헤더와 전체 HTML은 보관하지 않는다. 숫자 코드가 없는 응답도 짧은 본문으로 확인할 수 있다.

후속 Render 경고와 기존 `mapSelection.httpDiagnostics`, Geode의 `[SurvivalMap]` 상세 로그에 두 필드가 포함된다. 배포 후 실제 숫자 코드·본문을 확인해야 필요한 운영 접속 조치를 정할 수 있다. 테스트의 `1006`·`1010`·`1015`·`1020`은 다양한 응답을 검증하는 fixture이며 운영 응답에서 관측한 코드가 아니다.

개발 환경의 읽기 전용 비교에서는 기존 빈 User-Agent가 정상 GD 목록을 반환했지만 `Corum-Ranked/0.5` 및 Node 기본 User-Agent는 Cloudflare HTML 403을 반환했다. 이 환경은 관리형 프록시를 사용하며 Render와 Node 버전·발신 경로도 다르다. 따라서 헤더 교체를 해결책으로 배포하지 않고 기존 요청을 유지한다. 브라우저 challenge 우회, 새 프록시 배포 또는 운영 서버 환경 변경은 수행하지 않았다. 이 후속 패치 역시 운영 접근 거절의 해결을 주장하지 않는다.

로컬 검증: 전체 workspace 소스·테스트 타입검사 통과, 맵 제공자와 PGlite 경기 통합 테스트 **45개 통과**. 추가 검사는 텍스트 코드 추출·본문 길이 제한·HTML/challenge/관련 없는 본문 제외 및 DB 재시도 복구 시 진단 보존을 포함한다. 서버 전용 beta64 핫픽스로 클라이언트 재설치·R2 패키지 재게시·DB 마이그레이션·Apps Script 작업은 필요 없다. 전체 Geode 빌드, 실기기 및 배포 후 Render의 정상 맵 선정은 검증하지 않았다.

## Render ASN 차단 대응: 인증된 GD 중계

후속 운영 로그에서 실제 `edgeErrorCode=1005`를 확인했다. 사용자 승인에 따라 GD 검색·다운로드 두 요청을 사용자 소유 Cloudflare Worker로 전달할 수 있는 설정을 추가했다. 후보 선정·다운로드 검증·경기 권한은 NestJS 서버에 남고, 중계 인증키는 GD에 전달하지 않는다. 설정되지 않으면 기존 직접 조회를 사용한다.

Worker 이름은 `corum-ranked-gd-relay`, Worker Secret은 `GD_RELAY_TOKEN`, Render 변수는 `SURVIVAL_GD_RELAY_ORIGIN`·`SURVIVAL_GD_RELAY_TOKEN`이다. 실제 계정에서 Worker와 Render 설정을 적용해야 중계가 활성화된다. 전체 설치·점검 방법은 [Survival GD 중계 설정](survival-gd-relay.md)을 따른다. 이 변경은 서버 전용 beta64 패치이며 운영 배포나 실기기 접속 성공을 주장하지 않는다.
