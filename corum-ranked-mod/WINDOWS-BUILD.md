# Windows 로컬 빌드

기본 배포 빌드는 [Build Corum Ranked Debug](https://github.com/ybaf100/Corum-Ranked/actions/workflows/build-ranked-debug.yml)
GitHub workflow에서 생성한다. beta.43의 로컬 빌드 한정 설정은 사용자 요청으로 철회했다.
아래 스크립트는 원하는 경우 사용할 수 있는 Windows 로컬 빌드 방법이다.

## 처음 한 번 준비

1. [Visual Studio 2022 Build Tools](https://visualstudio.microsoft.com/downloads/) 또는
   Visual Studio 2022를 설치한다. **Desktop development with C++**, Windows SDK,
   **C++ Clang tools for Windows**, **C++ CMake tools for Windows**를 선택한다.
2. Git과 [Geode CLI](https://github.com/geode-sdk/cli/releases/latest)를 설치하고
   새 PowerShell 창에서 `git --version`, `geode --version`이 실행되는지 확인한다.
   CLI는 Scoop의 `scoop bucket add extras`, `scoop install geode-sdk-cli`로도 설치할 수 있다.
   Geode CLI를 처음 설치했다면 `geode config setup`으로 초기 설정을 완료한다.
3. `Corum-Ranked` 저장소의 최신 main을 내려받거나 기존 checkout에서 `git pull --ff-only`한다.

## 빌드

저장소 루트에서 PowerShell로 실행한다.

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\corum-ranked-mod\tools\build-windows.ps1
```

스크립트는 x64 VS 환경을 준비하고 `%LOCALAPPDATA%\CorumRankedBuild\geode-sdk`에
빌드용 SDK를 설치한다. manifest의 Geode 버전(`5.8.2`)과 Windows 바이너리를
맞춘 뒤, 평소 CI와 같은 Clang/Ninja Release 구성으로 컴파일한다. Geode CLI의
SDK 경로도 이 폴더로 설정한다. 이 과정에서 프로젝트 의존성 다운로드가 필요하다.

기본 빌드는 Debug Bot/VOICE 도구를 포함하며 결과는 다음과 같다.

```text
corum-ranked-mod\dist\windows-debug\hwanhee1.corum_ranked.geode
```

GD를 종료한 상태에서 이 파일을 `Geometry Dash\geode\mods`에 넣는다. 기존
같은 ID의 `.geode` 파일과 중복 설치하지 않는다. 에셋팩은 변경되지 않았으므로
새 이미지 ZIP을 만들거나 업로드할 필요가 없다. 이미 설치된 이미지팩은 계속 사용한다.

Debug 도구를 제외한 구성은 같은 명령 뒤에 `-Production`을 붙인다. 결과는
`dist\windows-production`에 생긴다. 메모리가 부족하면 `-Jobs 2`를 붙인다.
빌드 실패 시 PowerShell의 첫 compiler error부터 확인한다. 서버 exact gate도
beta.43이므로 이전 beta.42 클라이언트는 업데이트가 필요하다.
