# v0.4.0-alpha.5 최초 배포 체크리스트

현재 `Code.gs`와 `RankedConfig.gs`를 한 번도 반영하지 않은 최초 배포 기준이다.

## 사용자가 해야 하는 일

1. 전달받은 전체 소스 ZIP을 저장소 루트에 풀고 GitHub에 전체 업로드한다.
2. 기존 Spreadsheet의 Apps Script 편집기에서 현재 `Code.gs`를
   `apps-script/Code.gs` 전체 내용으로 교체한다.
3. 같은 Apps Script 프로젝트에 `RankedConfig.gs` 파일을 새로 만들고
   `apps-script/RankedConfig.gs` 전체 내용을 붙여 넣는다.
4. `appsscript.json`을 표시해 저장소 파일과 맞춘다.
5. `setupCorumIntegration()`을 한 번 실행한다.
6. `setupCorumRankedConfig()`을 한 번 실행한다.
7. 기존 맵 시트(`sheet1`) 오른쪽에 생긴 `Ranked Pool (1~6)`과 `Qualifying %`를
   같은 맵 행에 입력한다. Pool이 빈 행은 Ranked에서 제외된다.
8. 새로 생긴 `Ranked Tiers`, `Ranked CSMP Seed`, `Ranked Allowed Mods`,
   `Ranked Config`의 미확정 운영값을 입력한다. 값을 임의로 정하지 않는다.
9. `/exec?action=ranked_config` 응답의 `validation.valid=true`를 확인하고 마지막에만
   `enabled=TRUE`로 바꾼 뒤 Apps Script를 새 버전으로 재배포한다.
10. Neon Free DB를 만들고 `ranked/migrations/0001_initial_ranked.sql`을 한 번 실행한다.
11. Koyeb Free Web Service를 GitHub 저장소에 연결하고
    `ranked/docs/free-hosting-koyeb-neon.md`의 command/환경변수를 입력한다.
12. Koyeb `/health`, `/ready`, `/api/ranked/config`를 확인한다.
13. Geode alpha build를 설치하고 모드 설정의 서버 URL에 Koyeb HTTPS base URL을 넣는다.
14. 반드시 테스트 계정으로 Debug Bot Match부터 확인한다. 이 버전의 Bot Match도
    MMR/Score/Placement/W/L에 실제 반영된다.

## source에서 직접 바꿔야 하는 값

코드에 production secret을 넣을 필요는 없다. 배포 화면/Spreadsheet에서만 다음을 정한다.

- Spreadsheet: Seed MMR, 티어 경계, K-factor, Placement 경기 수, timeout/실패 정책
- Koyeb secret: Neon `DATABASE_URL`, `RANKED_SESSION_TOKEN_SECRET`
- 선택: Discord webhook과 relay 설정
- alpha Bot: `ENABLE_DEBUG_BOT_MATCH=true`, `DEBUG_BOT_PASSWORD=2008`

`2008`은 개발 gate일 뿐 계정 인증 수단이 아니다. 정식 출시 전에 Bot 기능 전체를 끈다.

## 이후 파일 변경 시 적용 위치

| 저장소 경로 | 사용자가 실제로 반영할 곳 |
| --- | --- |
| `apps-script/Code.gs` | 기존 Apps Script의 `Code.gs` 전체 교체 후 새 배포 |
| `apps-script/RankedConfig.gs` | 같은 Apps Script의 동명 파일 전체 교체/생성 후 새 배포 |
| `ranked/apps/server/**`, `ranked/packages/**`, `ranked/migrations/**` | GitHub push → Koyeb 재배포; 새 migration이 생긴 버전만 DB에 순서대로 실행 |
| `corum-ranked-mod/**` | GitHub Actions/로컬에서 새 `.geode` 빌드 후 설치·배포 |
| `.github/workflows/**` | GitHub push만 하면 적용 |
| `ranked/docs/**` | 배포 명령/운영 절차 확인용 |

앞으로 전달본은 일부 overlay가 아니라 매 버전 전체 소스 ZIP이며, 이 ZIP 역시 저장소
루트에 바로 덮어쓸 수 있는 구조로 만든다.
