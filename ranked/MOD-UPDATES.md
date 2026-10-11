# 모드 업데이트 배포

소스와 CI는 `ybaf100/Corum-Ranked`에 그대로 둔다. 공개 배포 저장소는
`ybaf100/corum-ranked-downloads`이며 모드 패키지와 검증용 JSON만 업로드한다.
이미지팩의 `corum-ranked-assets` 저장소는 그대로 사용한다.

## 최초 설정

1. GitHub에서 `ybaf100/corum-ranked-downloads`를 **Public**으로 만들고 README만 넣는다.
   Corum-Ranked 소스를 이 저장소에 복사하지 않는다.
2. 이 배포 저장소 하나에만 **Contents: Read and write** 권한이 있는 fine-grained PAT를 만든다.
3. 소스 저장소의 Settings → Secrets and variables → Actions → Secrets에
   `RANKED_RELEASE_TOKEN`으로 저장한다. 서버나 모드에 토큰을 넣지 않는다.
4. 같은 화면의 Variables에 `RANKED_PUBLISH_RELEASES`를 `true`로 설정한다.
   이후 main의 모드 변경 push는 Debug와 Release를 각각 빌드·검증한 뒤 자동 배포한다.
   설정 전에는 기존 Debug artifact 빌드만 실행되고 자동 Release 빌드는 생략한다.

v0.5.0-beta.56이 최초 업데이트 기능 포함 버전이다. 기존 beta55 이하는 이 기능이
없으므로 beta56을 한 번 직접 설치해야 한다. 이 변경 작업에서는 저장소 생성,
PAT/Actions 설정 및 실제 Release 발행을 수행하지 않는다.

## 릴리즈 규칙

- Debug 태그: `debug-v0.5.0-beta.56`
- Debug 파일: `Corum-Ranked-v0.5.0-beta.56-debug.geode` + `debug.json`
- Release 태그: `release-v0.5.0-beta.56`
- Release 파일: `Corum-Ranked-v0.5.0-beta.56-release.geode` + `release.json`
- 태그와 파일은 불변이다. 발행된 같은 버전의 파일을 교체하지 말고 버전을 올린다.
- Debug는 prerelease이며 Release와 파일명·태그·패키지 내부 채널 표시가 다르다.
- 패키지에는 Windows/iOS/Android32/Android64 바이너리가 모두 있어야 한다.
- Release는 draft로 파일을 모두 업로드한 뒤 공개된다. 진행 중인 draft는 서버에 지정하지 않는다.
- 사용자용 CI artifact ZIP에서는 기존 `hwanhee1.corum_ranked.geode`를 설치한다.
  배포용 개별 파일과 내용·SHA-256은 같다. artifact ZIP 자체를 업데이트 대상으로 올리지 않는다.
- 배포 파일명에 버전·채널을 붙이는 이유는 Debug와 Release의 수동 다운로드도 구분하기 위해서다.
  자동 업데이트는 이 이름을 설치 파일명으로 쓰지 않고, 현재 모드의 `getPackagePath()`를
  교체하므로 기존 `hwanhee1.corum_ranked.geode` 파일명과 설치 위치를 유지한다.
- 이미지팩은 PNG·팩 명세·라이선스·팩 생성기가 바뀌었을 때만 다시 패키지한다.

## 서버에 받을 버전 지정

환경변수를 설정하지 않은 채널은 서버 코드의 `REQUIRED_RANKED_CLIENT_VERSION`을
대상으로 사용한다. `/api/ranked/config`가 해당 버전의 공개 릴리즈에서 `debug.json`과
`release.json`을 자동으로 읽어 다운로드 명세를 제공한다. 예를 들어 서버가 beta57을
요구하면 `debug-v0.5.0-beta.57/debug.json`과 `release-v0.5.0-beta.57/release.json`만
조회한다. `latest`나 다른 버전·채널로 넘어가지 않는다.

서버 시작 때 비동기로 조회를 준비하고, 성공한 명세는 프로세스 동안 캐시한다.
릴리즈가 아직 없거나 조회가 실패하면 해당 채널을 생략하고 20초 이후 다시 조회할 수
있다. 각 요청의 동시 조회를 합치고 타임아웃을 적용한다. 모드가 RETRY로 config를
다시 받으면 공개된 명세가 반영된다. 경기 heartbeat와 다음 라운드는 이 조회를 하지 않는다.

채널별로 별도 버전을 고정하려면 **먼저 공개 Release와 파일이 존재하는지 확인한 뒤**
선택적으로 서버 환경변수 `RANKED_MOD_UPDATES_JSON`을 설정한다. 명시한 채널의
설정은 자동 조회보다 우선하며 서버가 다른 버전으로 바꾸지 않는다. 발행된 `debug.json`과
`release.json`을 하나의 객체로 합쳐 넣으면 된다. 예시의 SHA·크기는 실제 JSON 값으로 바꾼다.

```json
{
  "debug": {"version": "v0.5.0-beta.56", "sha256": "64자리 SHA-256", "sizeBytes": 12345678},
  "release": {"version": "v0.5.0-beta.56", "sha256": "다른 64자리 SHA-256", "sizeBytes": 12345000}
}
```

두 채널의 버전을 다르게 지정할 수도 있다. 미설정 채널은 서버 코드의 exact client
version gate와 해당 릴리즈의 명세를 사용하므로 버전마다 SHA·크기를 환경변수에 복사할
필요는 없다. 대상 릴리즈의 두 파일을 모두 업로드한 뒤 공개해야 한다.

모드 버전을 바꿀 때는 mod.json, npm 패키지·lockfile, 서버 fallback gate를 함께
올린다. 이미 진행 중인 경기의 heartbeat·attempt·다음 라운드에는 새 버전 검사를
넣지 않는다. 로비 새 입장과 새 매칭 버튼의 서버 요청에서만 검사한다.
배치 새 시작과 Debug 봇 매치 생성에도 같은 검사 경계를 적용한다.

## 게임에서의 순서

1. Ranked 입장 또는 매칭 버튼 → 현재 채널의 버전 검사. 검사 전용 안내 문구는 표시하지 않는다.
2. 불일치 → 모드 업데이트 화면. 버전·SHA-256·크기·채널·현재 플랫폼을 검증해 패키지를 교체한다.
3. 다운로드 완료 → **사용자가 Restart를 눌러야** Geode Restart가 실행된다.
4. 재실행 후 이미지팩 + 음악을 **추가 다운로드** 한 화면·한 버튼으로 확인한다.
5. 모든 필수 다운로드가 준비되면 서버 세션을 만들고 Ranked에 진입한다.

업데이트를 게임 도중 다운로드하거나 적용하지 않는다. 이미 입장한 구버전 세션은
진행 중인 경기를 계속할 수 있고 새 매칭은 현재 버전으로 검사한다.
실제 서버 프로세스 재시작·네트워크 공백까지 무중단으로 만드는 기능은 아니다.
iOS Restart는 Geode가 런처에 `geode://relaunch`를 요청하는 공식 동작이며,
런처가 지원하지 않으면 Geode의 수동 재시작 안내를 따른다.

beta56의 DB 추가 migration은 `0020_deathmatch_native_death.sql`이다.
킬로그 표시용으로 사망 여부만 저장하며 기존 점수·Clear·프로토콜 v2는 유지한다.
