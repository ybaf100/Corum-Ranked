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
- NestJS/PostgreSQL 서버가 매칭, 시간, attempt 승인, 점수, 승패, MMR을 최종 판정합니다.
- Apps Script는 운영 설정만 제공하며 실시간 경기 상태를 저장하지 않습니다.
- Ranked Pool과 Qualifying은 별도 문서가 아니라 기존 맵 시트(`sheet1`)의 오른쪽 두 열에서 관리합니다.
- `v0.4.0-alpha.9`의 Debug Bot Match는 개발용이지만, 테스트 기간에는 일반 PvP와 동일하게 MMR·Ranked Score·Placement·W/L에 반영됩니다.
- 미확정 MMR Seed·티어 경계·K-factor·timeout·매칭 폭·실패 정책은 운영자가 입력하기 전까지 queue가 fail-closed 상태를 유지합니다.
- Production secret은 저장소에 포함하지 않습니다.

구현·테스트·배포 안내는 [`ranked/README.md`](ranked/README.md)를 참고하세요.


---


> v0.4.0-alpha.9 policy: the Ranked allowlist checks currently enabled + loaded mods only. Installed-but-disabled mods do not block Ranked; required mods such as CBF must still be active.
