# Release builds

`corum-integration-mod/mod.json`의 `version`이 모드, 설치기와 Release 파일명의 단일 버전 기준입니다.

## CI artifacts

- `.github/workflows/build-mod.yml`
  - 기존 Apps Script·버전 비교·점수 테스트를 실행합니다.
  - Win64, iOS, Android32와 Android64를 빌드하고 하나의 패키지로 combine합니다.
  - `hwanhee1.corum_integration.geode`를 artifact로 게시합니다.
- `.github/workflows/build-installer.yml`
  - 설치기 핵심 로직 테스트를 실행합니다.
  - .NET 8 WinForms 앱을 self-contained, single-file, win-x64로 publish합니다.
  - `Corum-Integration-Installer-v{VERSION}.exe`를 artifact로 게시합니다.

두 workflow는 수동 실행과 재사용 가능한 `workflow_call`을 모두 지원합니다. 기존 브랜치 push 모드 빌드도 유지됩니다.

## GitHub Release

`.github/workflows/release.yml`은 버전 태그 push 또는 수동 실행으로 두 빌드를 호출하고 다음 파일을 같은 GitHub Release에 게시합니다.

- `Corum-Integration-Installer-v{VERSION}.exe` — Windows 자동 설치·업데이트
- `hwanhee1.corum_integration.geode` — 모든 포함 플랫폼의 수동 설치 패키지

수동 실행은 `main`에서만 허용되며 `mod.json`의 버전을 자동으로 사용합니다. 태그 실행 시 태그 이름은 반드시 그 버전과 일치해야 합니다. 게시 직전 `.geode`의 ZIP 구조, Mod ID와 버전도 다시 검증합니다.

Release 제목은 `mod.json`의 버전으로 설정합니다. 본문은
`release/RELEASE-NOTES-TEMPLATE.md`의 다운로드 안내 뒤에
`corum-integration-mod/changelog.md`에서 같은 버전의 수정 내역만 붙여
자동 생성합니다. 기존 Release를 다시 실행해도 asset과 함께 제목과
본문을 현재 형식으로 갱신합니다.
