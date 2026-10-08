# Ranked 이미지팩 배포 및 업로드

## 저장소와 버전 규칙

- 모드 소스와 빌드는 `ybaf100/Corum-Ranked`에 유지합니다.
- 이미지팩 호스팅은 별도 **공개** 저장소 `ybaf100/corum-ranked-assets`를 사용합니다. 비공개 저장소의 로그인 링크는 클라이언트가 받을 수 없습니다.
- 서버의 `client.assets.assetPackVersion`은 이미지팩 릴리스의 **태그와 정확히 같아야** 합니다. 초기 태그는 `visuals-v1`입니다. `latest`는 조회하지 않습니다.
- 실제 다운로드 주소: `https://github.com/ybaf100/corum-ranked-assets/releases/download/visuals-v1/Corum-Ranked-Assets.zip`
- 릴리스에 붙이는 ZIP 파일명은 항상 **`Corum-Ranked-Assets.zip`**입니다. 대소문자와 확장자를 바꾸지 않습니다. GitHub가 자동 생성한 Source code ZIP이나 Actions artifact 바깥 ZIP은 업로드하지 않습니다.
- 공개한 태그와 ZIP은 변경하지 않습니다. 이미지가 하나라도 달라지면 `visuals-v2` 등 **새 태그**와 새 ZIP을 만들고 서버의 버전·SHA-256·크기를 함께 갱신합니다. 같은 태그의 파일을 교체하거나 태그를 이동하지 않습니다.
- 이 이미지팩 버전은 모드 버전과 별개입니다. 이미지 교체만으로 GD 모드 버전을 올릴 필요는 없습니다. 팩 포맷/필수 파일 목록을 바꾸는 경우는 먼저 호환 클라이언트를 배포해야 합니다.
- 서버는 릴리스의 게시 여부와 무관하게 지정한 버전만 요청합니다. GitHub의 “Set as latest release” 표시나 다른 릴리스의 날짜는 선택에 영향을 주지 않습니다.

## 최초 업로드 순서

1. GitHub에서 `ybaf100/corum-ranked-assets`를 **Public**으로 생성합니다. 빈 저장소라면 README를 포함해 최초 커밋을 만듭니다. 이 작업은 Corum-Ranked-Web이나 Geode 소스를 옮기는 작업이 아닙니다.
2. `Corum-Ranked`의 이번 후보 HEAD에 대한 **Build Corum Ranked Debug** 자동 CI가 모두 성공했는지 확인합니다. `Corum-Ranked-Assets-visuals-v1` artifact를 다운로드하고 바깥 ZIP을 풀면 `Corum-Ranked-Assets.zip`과 `asset-pack-metadata.json`이 나옵니다. 모드 패키지 artifact와 이미지팩 artifact는 서로 다릅니다. 수동 Release workflow 실행은 필요 없습니다.
3. 별도 에셋 저장소의 **Releases → Draft a new release**에서 태그 `visuals-v1`을 만들고 생성된 내부 `Corum-Ranked-Assets.zip`을 그대로 첨부합니다. `asset-pack-metadata.json`도 첨부하면 운영 확인에 편리합니다. 릴리스 본문에 버전·SHA-256·크기·이미지 변경 내용을 기록하고 게시합니다.
4. 로그아웃 상태에서도 위 태그 다운로드 URL이 ZIP을 반환하는지 확인합니다. 다운로드한 ZIP의 SHA-256/바이트 크기가 `asset-pack-metadata.json`과 일치해야 합니다. 이미지 61개, LICENSE, `asset-manifest.json`을 루트에 그대로 유지합니다. 다시 압축하면 ZIP의 해시/크기가 달라집니다.
5. 운영 Apps Script에 이 브랜치의 `apps-script/RankedConfig.gs` 변경을 반영하고 `setupCorumRankedConfig()`를 한 번 실행해 **누락된 네 설정 행만 추가**합니다. 기존 운영 값은 유지됩니다. 웹 앱 배포도 최신 소스로 갱신합니다. 원격 서버 역시 이 브랜치의 설정 타입/검증 코드를 반영해야 합니다.
6. `Ranked Config` 시트에서 아래 세 값을 **생성된 metadata의 `sheetSettings`에서 복사**한 뒤 `rankedAssetPackEnabled`를 `TRUE`로 켭니다. 문서에 예시 해시/크기는 적지 않습니다. 실제 ZIP의 값만 사용합니다.

| 설정 키 | 입력 값 |
|---|---|
| `rankedAssetPackVersion` | `visuals-v1` (릴리스 태그와 정확히 동일) |
| `rankedAssetPackSha256` | metadata의 64자리 `rankedAssetPackSha256` |
| `rankedAssetPackSizeBytes` | metadata의 정수 `rankedAssetPackSizeBytes` |
| `rankedAssetPackEnabled` | 릴리스/다운로드 검증 후 `TRUE` |

7. 운영 `/api/ranked/config`에서 `client.assets`의 네 값과 새 config generation을 확인합니다. 서버의 config refresh가 적용된 후 메뉴에 진입해야 합니다. 기존 서버는 모르는 `assets` 필드를 그대로 내보낼 수 있지만, 배포된 검증 코드로도 확인하는 것이 이 변경의 검증 범위입니다.
8. 첫 설치는 **이미지팩 신규 다운로드**, 기존 설치에서 버전이 달라지면 **이미지팩 업데이트** 화면이 뜹니다. 업데이트에는 설치 버전 → 서버 버전이 표시됩니다. **모두 다운로드** 한 번으로 ZIP 한 개와 필요한 기존 음악 리소스를 내려받습니다. 이미지 파일별 다운로드/확인은 요구하지 않습니다. 성공 후 **계속**을 누르면 Ranked 메뉴로 들어갑니다.

음악은 기존 GD Song ID 리소스이므로 음악까지 전부 이미지 ZIP에 넣지는 않습니다. 이미지 다운로드 실패·해시/크기 불일치·설치 실패 시 계속 버튼으로 우회하지 못합니다. 검증이 끝나기 전에는 기존 설치 팩을 유지하고, 실패한 새 팩은 재시도합니다. 새 모드 패키지에서는 큰 이미지 61개를 제외하며 폰트와 작은 GD 기본 화면은 유지됩니다. 에셋팩 기능을 꺼 둔 구형 config로도 기본 화면은 사용할 수 있습니다.

## 다음 이미지팩 업데이트

1. 이미지 원본은 `Corum-Ranked/corum-ranked-mod/resources`에서 관리합니다. `asset-pack.json`의 `assetPackVersion`을 `visuals-v2`로 바꿉니다. 현재 팩 포맷 1은 정확히 61개 이미지 목록을 사용합니다. 원본 폰트 파일과 모드 바이너리는 팩에 넣지 않습니다.
2. 자동 CI의 새 에셋 artifact를 받거나, 전체 checkout에서 `python3 corum-ranked-mod/tools/build-asset-pack.py --version visuals-v2 --output asset-pack-dist`를 실행합니다. ZIP을 재현 가능하게 생성하고 해시/크기도 함께 계산합니다.
3. `python3 corum-ranked-mod/tests/verify-asset-package.py --pack asset-pack-dist/Corum-Ranked-Assets.zip --metadata asset-pack-dist/asset-pack-metadata.json`으로 검증합니다.
4. 별도 에셋 저장소에 **새 태그** `visuals-v2` 릴리스를 만들고 같은 고정 파일명으로 새 ZIP을 게시합니다. 먼저 업로드/검증하고, 그다음 서버 설정의 버전·해시·크기를 같이 바꿉니다. 서버를 먼저 바꾸면 아직 없는 릴리스를 요청해 다운로드가 실패합니다.
5. 이전 릴리스는 삭제하지 않습니다. 이전 서버 설정으로 되돌려야 할 때는 그 릴리스 metadata의 세 값을 함께 되돌립니다. 클라이언트는 서버가 지정한 이전 태그를 다시 받을 수 있습니다.

이 문서는 업로드 절차입니다. 외부 저장소 생성/릴리스 게시와 운영 Apps Script/설정 배포는 자동으로 실행하지 않습니다. Pause 후보의 실기기 성공 여부도 이미지팩 CI 성공과 별도로 확인해야 합니다.
