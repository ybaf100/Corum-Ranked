# Corum Ranked

이 디렉터리는 첨부 구현명세 v0.3을 기준으로 한 Corum Ranked의 서버 권위 구현입니다. 기존 `corum-integration-mod`와는 런타임, UI, API 상태, 빌드 산출물을 공유하지 않습니다.

## 구성

| 경로 | 역할 |
|---|---|
| `packages/rules` | tier/pool, scoring, timer, 2-Clear, BO3, deathmatch, MMR, allowlist의 순수 domain |
| `apps/server` | NestJS REST API, PostgreSQL transaction, config snapshot, session/queue/match, Discord outbox |
| `migrations/0001_initial_ranked.sql` | Ranked 전용 PostgreSQL schema |
| `../apps-script/RankedConfig.gs` | 기존 맵 시트의 Ranked 열 + 별도 운영 설정과 `ranked_config` 조회 API |
| `../corum-ranked-mod` | 별도 Geode 모드 `hwanhee1.corum_ranked` |

## 로컬 검증

Node.js 22 이상과 C++23 컴파일러가 필요합니다.

```bash
npm ci --prefix ranked
npm run typecheck --prefix ranked
npm test --prefix ranked
npm run build --prefix ranked
npm run test:harness --prefix ranked
node apps-script/validate-ranked-config.mjs
node apps-script/validate-record-sheet-compat.mjs
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror \
  corum-ranked-mod/tests/DomainTest.cpp \
  corum-ranked-mod/src/domain/EnvironmentPolicy.cpp \
  corum-ranked-mod/src/domain/HudPresentation.cpp \
  corum-ranked-mod/src/domain/RenderFpsMeter.cpp \
  corum-ranked-mod/src/domain/ServerClock.cpp \
  -o /tmp/corum-ranked-domain-test
/tmp/corum-ranked-domain-test
```

`test:harness`는 두 서버 세션을 실제 PGlite PostgreSQL 호환 DB에 연결해 Ready → private ban → round → MMR 흐름과 Round 3 Draw → 반복 deathmatch → 최종 승자를 검증합니다.

플레이 HUD는 화면 코너에 anchor되어 실제 frame update cadence FPS, viewer 기준 양쪽 authoritative Score, 서버 승인 Clear 체크 2개, snapshot Qualifying %, server deadline 보간 타이머를 표시합니다. 상대 현재 progress는 2-Clear 성공자가 상대의 LAST ATTEMPT를 기다리는 동안에만 서버가 허용하며, 이 telemetry는 영구 저장 및 판정에서 분리됩니다.

## 배포 순서

1. Google Spreadsheet에 최신 Apps Script 파일을 반영하되 기존 `setupCorumIntegration()`은 그대로 둡니다.
2. Apps Script 편집기에서 `setupCorumRankedConfig()`를 운영자가 명시적으로 한 번 실행합니다.
3. 기존 맵 시트의 `Ranked Pool (1~6)`/`Qualifying %` 열과 `Ranked Tiers`, `Ranked CSMP Seed`, `Ranked Allowed Mods`, `Ranked Config`의 미확정 값을 입력합니다. 별도 `Ranked Pool` 탭은 만들지 않습니다.
4. 설정 검증 오류가 0개인지 확인한 뒤 마지막에만 `enabled=TRUE`로 변경합니다.
5. PostgreSQL에 `migrations/0001_initial_ranked.sql`을 적용합니다.
6. `apps/server/.env.example`을 참고해 배포 환경변수를 secret manager에서 주입하고 서버를 배포합니다.
7. `/health`, `/ready`, `/api/ranked/config`를 확인합니다.
8. 별도 `build-ranked-mod.yml` 결과인 `hwanhee1.corum_ranked.geode`를 배포합니다. alpha.6부터 production base URL `https://corum-ranked.onrender.com`이 기본값/런타임 fallback으로 포함되어 수동 URL 입력이 필요하지 않습니다.
9. 두 테스트 계정으로 전체 harness 시나리오를 staging에서 재검증한 뒤 운영 queue를 엽니다.

운영 절차와 장애 시 동작은 [`docs/operations.md`](docs/operations.md), API 계약은 [`docs/api.md`](docs/api.md), 보안 경계는 [`docs/security.md`](docs/security.md)를 참고하세요.

무료 alpha 호스팅 순서는 [`docs/free-hosting-render-neon.md`](docs/free-hosting-render-neon.md),
기존 최초 배포 체크리스트는 [`docs/alpha-5-handoff.md`](docs/alpha-5-handoff.md),
Debug Bot Match 제거 방법은 [`docs/debug-bot-match.md`](docs/debug-bot-match.md)를 참고하세요.
최신 alpha.10 변경 파일·검증 결과는
[`docs/v0.4.0-alpha.13-report.md`](docs/v0.4.0-alpha.13-report.md)에 정리되어 있습니다.


> v0.4.0-alpha.13 policy: the Ranked allowlist checks currently enabled + loaded mods only. Installed-but-disabled mods do not block Ranked; required mods such as CBF must still be active.
