# Corum Integration

## Node IDs 모드를 필요로 합니다
- 모드를 적용하고 첫 게임 실행시, 오류가 뜬다면 이 모드가 설치되지 않은 것일 확률이 매우 매우 높습니다!!

## 설치 방법은 [가이드](https://app.notion.com/p/Corum-Integration-3b9f6f1f3d05808a9237efd47f49e9ce?source=copy_link)를 참고하세요.


## 모드의 주요 기능
- Geometry Dash에서 Corum List 정보를 확인하고 기록을 직접 제출할 수 있습니다.

## 세부 정보
- **Corum 맵 정보**
  - 레벨 화면에서 Corum 난이도, 현재 순위, 최대 포인트를 표시합니다.

- **기록 제출**
  - 종이비행기 버튼을 통해 현재 최고 기록을 바로 제출할 수 있습니다.
  - 제출 전 예상 포인트와 기존 확정 포인트를 확인할 수 있습니다.

- **일괄 제출**
  - 제출 가능한 Corum 기록을 자동으로 찾아 한 번에 제출할 수 있습니다.
  - 제출 전 대상 맵과 예상 총점을 확인할 수 있습니다.

- **포인트 시스템**
  - Corum의 포인트 계산식을 게임 내에서도 동일하게 적용합니다.
  - 기록을 제출한 시점을 기준으로 포인트가 확정됩니다.

- **Geometry Dash 계정 연동**
  - 별도의 회원가입이나 토큰 입력 없이 현재 Geometry Dash 계정을 사용합니다.

- **자동 동기화**
  - 게임 시작 시 Corum 맵 목록과 서버 정보를 자동으로 불러옵니다.
  - 게임 중 불필요한 네트워크 요청을 하지 않습니다.

## Requirements

- Geometry Dash 2.2081
- Geode 5.8.2
- **Node IDs**

---

## Corum Ranked (별도 시스템)

`hwanhee1.corum_ranked`는 기존 Corum Integration과 결합되지 않은 별도 Geode 모드입니다. Ranked 서버·DB·설정 API·UI·빌드 워크플로는 각각 `ranked/`, `apps-script/RankedConfig.gs`, `corum-ranked-mod/`, `.github/workflows/*ranked*`에 분리되어 있습니다.

- 기존 Integration의 맵 정보·기록 제출 런타임은 Ranked와 공유하지 않습니다.
- NestJS/PostgreSQL 서버가 매칭, 시간, attempt 승인, 점수, 승패, 숨김 MMR과 표시 LP를 최종 판정합니다. 클라이언트/API에는 MMR을 노출하지 않습니다.
- Apps Script는 운영 설정만 제공하며 실시간 경기 상태를 저장하지 않습니다.
- 일반 Corum Integration은 계속 `sheet1`을 사용하고, Ranked Pool과 Qualifying은 같은 레이아웃의 별도 `sheet2`에서 관리합니다.
- `v0.5.0-beta.5`의 Debug Bot Match는 개발 전용이며 정식 클라이언트 빌드에서는 기본적으로 제외됩니다. 개발 빌드의 Force Win/Lose도 일반 PvP와 같은 결과·LP/MMR 경로를 사용하지만 `PERFECT WIN`에는 포함되지 않습니다.
- 미확정 MMR Seed·티어 경계·K-factor·timeout·매칭 폭·실패 정책은 운영자가 입력하기 전까지 queue가 fail-closed 상태를 유지합니다.
- beta.3부터 `Ranked Tiers.Main Pool`이 실제 5맵 분포를 결정하며, CSMP Seed 티어명은 고정 Ranked 표시 티어와 독립적으로 추가할 수 있습니다.
- beta.4부터 상대 Hidden MMR이 MMR과 LP 이동량에 미치는 강도를 각각 설정할 수 있습니다. 실제 3-Clear 결정승만으로 무패 우승하면 기존 공식의 최대 MMR·LP 획득량과 노란 `PERFECT WIN` 결과가 적용됩니다.
- beta.5는 beta.4의 경기 규칙을 그대로 유지하면서 Ranked 전체 화면, LevelInfo 대기 게이트와 게임 HUD를 새 디자인 시스템으로 교체합니다. 최신 HUD는 플레이 배경 프레임과 사각 스크림 없이 글자 그림자만 사용하고, Paperlogy SemiBold를 기본 문구에, Black을 핵심 수치와 동작에 적용합니다. 커스텀 트로피 파일 위치는 `corum-ranked-mod/resources/CUSTOM-UI-ASSETS.md`에 정리되어 있습니다.
- Production secret은 저장소에 포함하지 않습니다.

구현·테스트·배포 안내는 [`ranked/README.md`](ranked/README.md)를 참고하세요.


---


> v0.5.0-beta.5 policy: the Ranked allowlist checks currently enabled + loaded mods only. Installed-but-disabled mods do not block Ranked; required mods such as CBF must still be active.
