# 게임 번역 편집

GitHub에서 아래 UTF-8 텍스트 파일을 수정하면 다음 빌드에 적용됩니다.

- [`ui.json`](ui.json): 메뉴, 설정, Solo Ranked, Survival, HUD, Pause, 결과, 관전, 관리자/Debug 안내 등 기존 번역 문구.
- [`achievements.json`](achievements.json): 도전과제 48종의 이름과 설명. 클라이언트와 서버가 같은 파일을 사용합니다.

각 키의 `en`과 `ko`를 수정합니다. 키와 도전과제 ID는 유지하세요.
`{}` 같은 자리표시자의 개수와 형식도 두 언어에서 유지해야 합니다. 줄바꿈은 `\n`으로 씁니다.
플레이어명, 레벨명, 곡명, 외부 서버의 알 수 없는 오류는 원본 데이터를 표시합니다.
LP/AP, 티어명 등 의도적으로 같은 표기를 쓰는 항목은 두 언어의 값이 같을 수 있습니다.

```json
"close": {
  "en": "CLOSE",
  "ko": "닫기"
}
```

프로젝트 루트에서 수동 생성/검사할 수 있습니다.

```sh
node ranked/tools/localization/build.mjs
node ranked/tools/localization/build.mjs --check
```

CMake는 구성할 때와 컴파일 전에 클라이언트 번역을 생성합니다. rules workspace의
`prebuild`, `pretest`, `pretypecheck`는 서버 번역을 생성합니다. 생성 파일은 리뷰 및 네이티브
테스트를 위해 함께 커밋하지만 직접 수정하지 않습니다. GitHub에서 JSON만 편집해도
빌드가 생성 파일을 갱신합니다. 로컬에서 작업할 때는 생성 결과도 함께 커밋하면
번역 적용 결과를 리뷰하기 쉽습니다.

- `corum-ranked-mod/src/domain/TranslationCatalog.generated.hpp`
- `ranked/packages/rules/src/achievement-translations.generated.ts`

클라이언트 생성 단계는 네 가지 활성 CorumSans 폰트의 charset에 필요한 글자를 추가합니다.
CI의 네이티브 검증도 번역을 먼저 생성합니다. CI는 실제 패키지의
SD/HD/UHD 폰트 및 메모리 예산을 별도로 검사합니다.
카탈로그만 수정해도 Release/Debug 빌드가 실행되도록 workflow 경로를 포함했습니다.
런타임 파일 다운로드나 네트워크 번역 서비스는 사용하지 않습니다.
