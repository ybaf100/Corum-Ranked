# Survival GD 조회 중계 설정

2026-10-12 Render 로그의 `edgeErrorCode=1005`는 GD 사이트가 발신 ASN을 차단했음을 뜻한다. [Cloudflare 공식 설명](https://developers.cloudflare.com/support/troubleshooting/http-status-codes/cloudflare-1xxx-errors/error-1005/). 요청 본문·후보 필터를 변경해도 이 네트워크 차단은 해소되지 않는다.

사용자가 권장 방향으로 진행하도록 승인해, 기존 Render 서버를 유지하고 사용자 소유 Cloudflare Worker가 GD 검색·다운로드를 전달하도록 구현했다. Worker는 경기·프로필·LP·후보 풀을 저장하거나 맵을 선택하지 않는다. NestJS가 후보를 무작위 선정하고 실제 GD 다운로드의 메타데이터·압축 해제된 오브젝트 데이터·해시를 검증해 경기 DB에 공통 맵을 확정한다.

## 1. Worker 생성

Cloudflare 대시보드에서 **Workers & Pages → Create application → Create Worker**(또는 Hello World로 시작)를 선택한다.

- 이름: **`corum-ranked-gd-relay`**
- 생성한 Worker의 **Edit code**를 연다.
- 기본 코드를 전부 바꾸고 [`ranked/tools/gd-relay/worker.mjs`](../tools/gd-relay/worker.mjs)의 내용을 그대로 붙인다. GitHub에서는 파일을 열어 Raw/Copy 기능으로 복사할 수 있다.
- **Deploy**를 누른다. ES module의 `export default`를 유지한다.

이 Worker는 기존 `corum-ranked-handoff`와 R2 다운로드 버킷·Worker에서 분리한다. R2 자격증명을 쓰거나 R2 binding을 추가할 필요는 없다. 로컬 Wrangler로 배포하려면 같은 디렉터리의 `wrangler.jsonc`를 사용할 수 있지만 대시보드 방식으로도 설정할 수 있다.

## 2. 중계 인증키 생성 및 Worker Secret 설정

새로운 무작위 인증키 한 개를 만들어 Worker와 Render에 같은 값을 넣는다. URL에 키를 붙이지 않고 코드·GitHub·채팅에 저장하지 않는다. 32~256자의 영문·숫자·`_`·`-`만 허용한다. R2 key, Ranked session key, Debug 비밀번호를 재사용하지 않는다.

PC에서 생성하는 예:

```powershell
# Windows PowerShell: 32바이트 난수 → 64자리 hex
$relayRandomBytes = New-Object byte[] 32
$relayRandomSource = [System.Security.Cryptography.RandomNumberGenerator]::Create()
$relayRandomSource.GetBytes($relayRandomBytes)
$relayRandomSource.Dispose()
[System.BitConverter]::ToString($relayRandomBytes).Replace('-', '').ToLowerInvariant()
```

macOS/Linux에서는 `openssl rand -hex 32`를 사용할 수 있다.

Worker의 **Settings → Variables and Secrets → Add**에서:

| 항목 | 값 |
|---|---|
| Type | **Secret** |
| Variable name | **`GD_RELAY_TOKEN`** |
| Value | 방금 생성한 인증키 |

저장하고 변경 내용을 배포한다. [Cloudflare Secret 설정 안내](https://developers.cloudflare.com/workers/configuration/secrets/).

Worker의 `https://corum-ranked-gd-relay.<본인-subdomain>.workers.dev/health`를 브라우저로 연다. `service=corum-ranked-gd-relay`, `protocol=v1`, **`configured=true`**가 나오면 코드와 Secret이 준비된 상태다. health는 GD 접속을 검사하지 않는다.

## 3. Render에 연결

기존 `corum-ranked` 서비스의 **Environment**에 아래 두 항목을 함께 추가한다.

| Render 변수 | 값 |
|---|---|
| **`SURVIVAL_GD_RELAY_ORIGIN`** | Worker의 실제 `https://…workers.dev` 주소. `/health`나 `/getGJLevels21.php` 없이 origin만 입력 |
| **`SURVIVAL_GD_RELAY_TOKEN`** | Worker의 `GD_RELAY_TOKEN`과 **동일한 인증키 값** |

저장하고 최신 main 서버를 재배포한다. 시작 로그에 아래가 나오면 서버가 중계를 선택한 것이다.

```text
[SurvivalGdTransport] Survival GD transport=relay
```

변수 이름은 Worker와 Render가 서로 다르지만 값은 같아야 한다. 둘 중 하나만 설정하면 서버 시작 시 설정 오류로 거절한다. 두 변수 모두 비어 있으면 기존 직접 GD 조회를 사용하므로 현재 Render의 1005 차단도 그대로다. 설정된 중계가 실패하면 막힌 Render 경로로 자동 우회하지 않는다.

## 4. 실제 접속 확인

Survival Debug 경기를 시작해 실제 맵 선정과 준비 화면을 확인한다. 서버가 중계를 통해 GD 검색·다운로드를 모두 수행한 뒤 기존 검증을 통과해야 `PREPARING`으로 진행한다. 기존 beta64 클라이언트로 가능하며 재설치나 버전·R2 패키지 변경은 없다.

서버 파일과 Node가 있는 환경에서는 별도로 아래 점검 도구를 쓸 수 있다. 필요한 환경 변수는 위 두 항목뿐이며 DB에 연결하거나 경기·LP를 만들지 않는다.

```bash
npm ci --prefix ranked
npm run build --prefix ranked
# 현재 프로세스 환경에 위 두 변수를 설정한 뒤 실행
node ranked/tools/gd-relay/probe.mjs
```

성공 시 `ok=true`, 실제 `levelId`, `stars`, `geometryBytes`, `geometrySha256`가 출력된다. 인증키는 출력하지 않는다. 이 점검은 실제 GD 맵을 다운로드·검증하므로 health보다 강한 접속 확인이다.

## 오류 해석

| 서버 경고 | 확인할 항목 |
|---|---|
| `GD relay UNAUTHORIZED (HTTP 401)` | Worker/Render 키 값 불일치 |
| `GD relay NOT_CONFIGURED (HTTP 503)` | Worker Secret 누락·형식 오류·미배포 |
| `GD relay INVALID_RELAY_RESPONSE` | 주소·Worker 코드·배포 여부 확인. HTML/login/redirect 응답도 거절 |
| `GD relay UPSTREAM_TIMEOUT/UPSTREAM_UNAVAILABLE` | Worker에서 GD 요청이 실패하거나 시간 초과 |
| `GD relay RESPONSE_TOO_LARGE` | GD 응답 크기 제한 초과 |
| `GD … HTTP 403; … edgeErrorCode=1005` | **Worker의 GD 발신 경로도 차단됨.** 실제 GD의 원래 Ray ID가 보존되므로 다시 진단 가능 |

Worker가 모든 GD 네트워크 차단을 해결한다고 보장할 수는 없다. 사용자 계정에서 배포 후 실제 검색·다운로드가 성공해야 운영 접속 해결로 확인할 수 있다. Worker에서도 1005가 나오면 GD 측 허용 요청이나 접속 가능한 호스팅이 필요하다.

## 요청 범위와 검증

- 인증된 POST 두 경로만 허용: `/getGJLevels21.php`, `/downloadGJLevel22.php`.
- 고정 HTTPS GD origin만 조회. 임의 URL·계정 API·검색어·중복/알 수 없는 form 필드·redirect는 거절한다.
- Rated 최신 검색의 일반 난이도 3~5 또는 Demon filter 1~5, 비 Platformer 길이, 최근 0~255페이지만 허용한다. 다운로드 ID 하한은 40,000,000이며 NestJS의 설정 하한이 더 높으면 서버가 추가 적용한다.
- 인증키·사용자 Cookie·임의 헤더는 GD에 전달하지 않는다. 검색 2 MiB·다운로드 24 MiB·요청 4 KiB 및 GD 요청 8초 제한을 적용한다. 큰 응답은 스트림으로 전달하며 제한 초과 시 중단한다.
- Cloudflare가 Worker 응답에 붙이는 헤더와 실제 GD 응답 헤더를 구분한다. 403·429 및 중계 설정/인증 오류는 기존 제공자·DB 재시도 구조에서 30초 대기하며, 오류 상태에서 경기 타이머를 시작하지 않는다.

## 변경 및 검증 범위

서버 전용 beta64 후속 패치다. DB 마이그레이션·Apps Script 변경·게임 클라이언트 빌드는 필요 없다. 운영 Cloudflare·Render 설정과 배포는 이 개발 환경에 계정 권한이 없어 직접 적용하지 않았다.

로컬 validate 결과:

- 전체 workspace 소스·테스트 TypeScript 검사 및 NestJS 서버 빌드 통과.
- 서버 환경·중계·기존 맵 제공자·경기 DB 통합 테스트 **81개 통과**. 중계 인증 실패 후 같은 경기의 복구, 준비/타이머 분리 및 Solo/Survival 프로필 불변을 검증했다.
- Worker 테스트 **13개 통과**. 인증·라우팅·필터·크기 제한·타임아웃/redirect·GD 진단 보존을 검증했다.
- 로컬 Worker handler를 관리형 개발 프록시에서 실행해 실제 GD 맵 **148595128 (6★)**을 검색·다운로드했다. NestJS 검증 결과 geometry는 **490391 bytes**, SHA-256은 `c3c6677a318b26e297c798d057adae5b53ea3469f9cbe13bfae4e26c77d70c4a`였다. 이는 실제 Cloudflare Worker의 발신 경로를 검증한 결과가 아니다.

실제 Cloudflare에서의 GD 접속과 실기기 플레이는 배포 후 별도로 확인해야 한다. 사용자 요청에 따라 전체 Geode 빌드·Android 패키지 검사와 GitHub Actions 완료 대기는 수행하지 않았다.
