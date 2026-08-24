# Corum Ranked v0.5.0-beta.6

## 변경 사항

- Ranked 로비 상단에 `English / 한국어` 언어 선택을 추가했습니다.
- 선택 언어는 저장되며 다음 실행에도 유지됩니다.
- 한국어 모드에서는 점수, 배치, 배치 완료, 승급, 강등, 강등 경고,
  승급전, 매칭, 밴, 결과, 기록, 관전 관련 기능 문구가 한국어로 표시됩니다.
- `CORUM RANKED`, `ROUND 1`을 포함한 Round 표기, `Qualifying`, 티어명,
  LP, 플레이어명과 맵명은 의도적으로 영어를 유지합니다.
- 우측 하단의 한글 `디버그` 글리프 검증 문구를 제거했습니다.
- 기존 beta.5 UI와 beta.4 경기 규칙, MMR/LP 계산, `PERFECT WIN` 판정은
  변경하지 않았습니다.

## 번역 수정 위치

한국어 표현은 아래 한 파일에 모여 있습니다.

`corum-ranked-mod/src/ui/RankedTranslations.hpp`

`kKoreanTranslations`의 오른쪽 한국어 문자열을 수정하면 됩니다. 새 한글
음절을 추가할 경우 Geode 비트맵 폰트에 포함되도록 `corum-ranked-mod/mod.json`의
`CorumSansSemiBold`, `CorumSansBold`, `CorumSansBlack` charset에도 해당 문자의
10진수 Unicode 코드 포인트를 추가해야 합니다.

## 적용

패치 ZIP은 상위 감싸기 폴더가 없습니다. 프로젝트 최상위에서 압축을 풀어
동일 경로의 파일을 덮어쓴 뒤 서버와 클라이언트를 함께 beta.6으로 빌드합니다.
