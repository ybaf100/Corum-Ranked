# Corum Ranked

이 디렉터리는 `corum-ranked-v0.5` 규칙을 구현한 Corum Ranked의 서버 권위 구현입니다. 기존 `corum-integration-mod`와는 런타임, UI, API 상태, 빌드 산출물을 공유하지 않습니다.

## 구성

| 경로 | 역할 |
|---|---|
| `packages/rules` | tier/pool, scoring, 5분 timer, 3-Clear, BO3, deathmatch, Placement, MMR, allowlist의 순수 domain |
| `apps/server` | NestJS REST API, PostgreSQL transaction, config snapshot, session/queue/match/placement, Discord outbox |
| `migrations/0001_initial_ranked.sql` | Ranked 전용 PostgreSQL 초기 schema |
| `migrations/0002_attempt_start_leases.sql` | alpha.33 visual Start Lease 영속화 / attempt 전송 경합 방지 |
| `migrations/0005_v050_rating_system.sql` | 숨김 MMR/표시 LP, 배치 결과, 승급전, 강등 경고, 연승/연패 상태 |
| `migrations/0006_v050_dynamic_csmp_tiers.sql` | 운영자 정의 CSMP Seed 티어명을 허용하는 정규화 제약 |
| `migrations/0007_solo_stage_placement.sql` | 5 Stage × 2 Map Placement Run/Map/Attempt 영속 상태와 exactly-once 결과 확인 |
| `../apps-script/RankedConfig.gs` | Ranked 전용 `sheet2`의 맵/Pool 열 + 별도 운영 설정과 `ranked_config` 조회 API |
| `../corum-ranked-mod` | 별도 Geode 모드 `hwanhee1.corum_ranked` |

## 로컬 검증

Survival M1 통합 및 beta.59 적용 절차는 [Survival M1 보고서](docs/survival-m1.md)를 참고한다.
Survival M3 beta.61의 경기 엔진, 랜덤 맵, DB/API 및 M4/M5 연결은 [Survival M3 문서](docs/survival-m3.md)를 참고한다.
Survival M4 beta.62의 실제 디버그 봇 경기, HUD, LP 정산 및 모드별 Admin은 [Survival M4 문서](docs/survival-m4.md)를 참고한다.
Survival M5 beta.68의 공개/축소 매칭, 디버그 봇 충원, 재접속, 개인 정산 및 운영 반영 순서는 [Survival M5 문서](docs/survival-m5.md)를 참고한다.
동일 서버·DB·Geode 모드에서 동작하며 운영 설정과 LP/리더보드는 모드별로 독립적이다.

Node.js 22 이상과 C++23 컴파일러가 필요합니다.

```bash
npm ci --prefix ranked
npm run typecheck --prefix ranked
npm test --prefix ranked
npm run build --prefix ranked
npm run test:harness --prefix ranked
node apps-script/validate-ranked-config.mjs
node --test apps-script/validate-survival-config.mjs
node apps-script/validate-record-sheet-compat.mjs
g++ -std=c++23 -Wall -Wextra -Wpedantic -Werror \
  corum-ranked-mod/tests/DomainTest.cpp \
  corum-ranked-mod/src/domain/AttemptScoring.cpp \
  corum-ranked-mod/src/domain/EnvironmentPolicy.cpp \
  corum-ranked-mod/src/domain/HudPresentation.cpp \
  corum-ranked-mod/src/domain/RenderFpsMeter.cpp \
  corum-ranked-mod/src/domain/ServerClock.cpp \
  -o /tmp/corum-ranked-domain-test
/tmp/corum-ranked-domain-test
```

`test:harness`는 두 서버 세션을 실제 PGlite PostgreSQL 호환 DB에 연결해 Ready → private ban → round → MMR 흐름과 Round 3 Draw → 반복 deathmatch → 최종 승자를 검증합니다.

## 버전 정책

Survival beta64의 맵 검색·대기 연출 및 R2 업데이트 설정은 [설정 안내](docs/survival-beta64-maps-r2.md)를 참고하세요. Ranked Debug/Release 배포는 `corum-ranked-download` R2 버킷을 사용하며, 최초 beta64는 수동 설치 후 이후부터 R2 자동 업데이트를 사용합니다.

Survival beta65의 Solo UI 재사용, 라운드 참가자·탈락 연출, 다음 맵 선택 대기 및 일시정지/항복 수정은 [진행 안내](docs/survival-beta65-ui-flow.md)를 참고하세요.

Survival beta66의 Solo 타이머·점수차 배치, 정수 점수 표시, 5초 참가자 화면 및 티어 후광은 [HUD 안내](docs/survival-beta66-hud-roster.md)를 참고하세요.

명시적인 긴급 핫픽스를 제외한 모든 패치는 `v0.5.0-beta.N`의 `N`을 1 올립니다. 버전을 올릴 때는 모드 manifest, 서버의 정확한 클라이언트 게이트, npm workspace/package-lock, 테스트 fixture, changelog와 release notes를 같은 beta 번호로 함께 갱신합니다. 긴급 핫픽스만 현재 beta 번호를 유지할 수 있습니다.

플레이 HUD는 화면 코너에 anchor되어 실제 frame update cadence FPS, viewer 기준 양쪽 authoritative Score, 서버 승인 Clear 체크 3개, snapshot Qualifying %, 현재 Round/Death Match와 server deadline 보간 타이머를 표시합니다. 배경 카드와 사각 스크림 없이 글자에만 검은 그림자를 적용하며, 일반 문구는 Paperlogy SemiBold, Score·타이머·Qualifying 값은 Paperlogy Black으로 표시합니다. 상대 현재 progress는 3-Clear 성공자가 상대의 LAST ATTEMPT를 기다리는 동안에만 서버가 허용하며, 이 telemetry는 영구 저장 및 판정에서 분리됩니다.

배치 미완료 사용자는 일반 PvP Queue와 Debug Bot 대신 별도 `PlacementService`의 싱글플레이 Run을 사용합니다. 시작 시 3개 또는 5개 Stage를 고르고 현재 Pool의 서로 다른 맵 2개씩 플레이하며, 배치 Map만 3분·2 Clear·상위 2 Attempt 합계(최대 500)를 사용합니다. 진행 중 provisional Map Score와 화면 Grade는 갱신될 수 있고, 영속 Map Grade는 Attempt 종료 ACK에서 확정됩니다. 자세한 상태·복구·API 계약은 [`docs/v0.5.0-beta.9-solo-placement.md`](docs/v0.5.0-beta.9-solo-placement.md)를 참고하세요.

클라이언트는 비허용 활성 모드, 필수 모드 누락·비활성, 버전 불일치와 CBF 설정 문제를 Paperlogy 기반 게임 내 경고창으로 구분해 안내합니다. Node IDs `v1.23.3` 이상은 `mod.json` dependency이자 `Ranked Allowed Mods`의 필수 항목입니다.

## 배포 순서

beta.47의 native 모드는 [Build Corum Ranked Debug](https://github.com/ybaf100/Corum-Ranked/actions/workflows/build-ranked-debug.yml)
GitHub workflow에서 빌드합니다. `Corum-Ranked-v0.5.0-beta.47-Debug` artifact의
`hwanhee1.corum_ranked.geode`를 사용합니다. Windows 로컬 빌드는
[`../corum-ranked-mod/WINDOWS-BUILD.md`](../corum-ranked-mod/WINDOWS-BUILD.md)의 선택 사항입니다.
서버와 클라이언트의 exact gate는 함께 beta.47을 사용합니다.

beta.47은 재매칭 전에 기존 매치의 만료 판정을 실행합니다. 양쪽 모두 재접속 유예 시간을
넘겨 이탈한 매치는 취소되고 재매칭할 수 있으며, 살아 있는 매치의 중복 생성은 계속 차단합니다.
세션 접속 만료와 별도로 기존 매치의 config snapshot에 저장된 재접속 정책을 적용합니다.

beta.46은 Pause 5초 이하를 빨간색으로 표시하고, 0초에 경기 전체를 자동 항복 처리합니다.
항복 안내는 Pause 중에만 표시되며 재개하면 닫힙니다. 미획득 칭호와 미달성 성장형 단계 버튼은 회색입니다.
설정의 개인 볼륨은 로비·매칭·결과 화면의 Ranked BGM에만 적용합니다.
플레이 중 GD 음악과 효과음, GD의 저장된 볼륨 설정은 변경하지 않습니다.

Ranked 계정 접속은 로비부터 한 연결만 허용합니다. 클라이언트는 기존 process scheduler에서
5초마다 접속을 갱신하고 정상 로비 종료 시 `/api/ranked/session/close`를 전송합니다.
강제 종료·네트워크 단절은 마지막 heartbeat에서 90초 후 만료되어 다른 기기의 접속을 허용합니다.
접속 제한 기능은 Spreadsheet 설정을 추가로 요구하지 않습니다.
beta.44의 포럼 기록 캐시는 서버 시작 시 `0014_forum_records.sql`을 자동 적용합니다.
API 키나 Spreadsheet 설정은 필요하지 않습니다. 상대 정보 카드는 매치 토큰으로 조회하며,
포럼 순위는 첫 프로필 생성 후 한 번 조회하고 이후 DB에서 읽습니다. 설정의
포럼 기록 갱신 버튼은 본인과 봇전 참고 프로필 CoJong을 갱신 대상으로 포함합니다.
사용자의 수동 갱신은 계정별 1시간에 한 번 가능합니다. 남은 시간은 설정에 표시되고,
쿨다운 동안 버튼은 회색으로 비활성화됩니다. 재접속이나 서버 재시작으로 초기화되지 않습니다.
최초 프로필 조회와 CoJong 자동 초기화는 이 수동 제한과 별개이며, API 오류 시 마지막 정상 순위를 보존합니다.
`0015_forum_refresh_cooldown.sql`은 서버 시작 시 자동 적용됩니다.

Ranked 메뉴 BGM은 `Ranked Config`의 기존 `rankedAudioMenuSongId`에
Newgrounds 또는 GD Music Library의 전체 Song ID를 입력합니다. `rankedAudioEnabled=TRUE`,
`rankedAudioMenuStartSeconds`(초) 설정과 다운로드 동작은
[`../corum-ranked-mod/MUSIC-LIBRARY-AUDIO.md`](../corum-ranked-mod/MUSIC-LIBRARY-AUDIO.md)를 참고하세요.

1. Google Spreadsheet에 최신 Apps Script 파일을 반영하되 기존 `setupCorumIntegration()`은 그대로 둡니다.
2. Apps Script 편집기에서 `setupCorumRankedConfig()`를 운영자가 명시적으로 한 번 실행합니다.
3. `sheet1`과 같은 레이아웃의 `sheet2`를 만든 뒤 그 탭의 `Ranked Pool (1~6)`/`Qualifying %` 열과 `Ranked Tiers`, `Ranked CSMP Seed`, `Ranked Allowed Mods`, `Ranked Config`의 미확정 값을 입력합니다. `Main Pool`은 실제 5맵 추첨을 이동시키며, 새 `CSMP Tiers` 이름에는 대응하는 Seed MMR이 반드시 필요합니다. `mmrOpponentStrengthWeight`와 `visibleScoreOpponentStrengthWeight`는 0~2 범위이며 0은 상대 강도 무시, 1은 기본, 2는 2배 반영입니다. 일반 Integration의 `sheet1`은 그대로 유지됩니다.
4. 설정 검증 오류가 0개인지 확인한 뒤 마지막에만 `enabled=TRUE`로 변경합니다.
5. 서버 시작 전에 `0001_initial_ranked.sql` → `0002_debug_bot_match.sql` → `0002_attempt_start_leases.sql` → `0003_debug_rating_playable_maps.sql` → `0004_alpha37_schema_hardening.sql` → `0005_v050_rating_system.sql` → `0006_v050_dynamic_csmp_tiers.sql` → `0007_solo_stage_placement.sql` → `0008_placement_beta9.sql` 순서로 적용합니다. DB를 초기화하므로 과거 PvP Placement 결과를 새 Map Score로 변환하지 않습니다.
6. `apps/server/.env.example`을 참고해 배포 환경변수를 secret manager에서 주입하고 서버를 배포합니다.
7. `/health`, `/ready`, `/api/ranked/config`를 확인합니다.
8. 별도 `build-ranked-mod.yml` 결과인 `hwanhee1.corum_ranked.geode`를 배포합니다. alpha.6부터 production base URL `https://corum-ranked.onrender.com`이 기본값/런타임 fallback으로 포함되어 수동 URL 입력이 필요하지 않습니다.
9. 두 테스트 계정으로 전체 harness 시나리오를 staging에서 재검증한 뒤 운영 queue를 엽니다.

운영 절차와 장애 시 동작은 [`docs/operations.md`](docs/operations.md), API 계약은 [`docs/api.md`](docs/api.md), 보안 경계는 [`docs/security.md`](docs/security.md)를 참고하세요.
Hidden MMR과 표시 LP에 실제로 반영되는 전체 요소·수식·예외는
[`docs/v0.5.0-beta.4-mmr-lp-policy.md`](docs/v0.5.0-beta.4-mmr-lp-policy.md)에 정리되어 있습니다.

무료 alpha 호스팅 순서는 [`docs/free-hosting-render-neon.md`](docs/free-hosting-render-neon.md),
기존 최초 배포 체크리스트는 [`docs/alpha-5-handoff.md`](docs/alpha-5-handoff.md),
Debug Bot Match 제거 방법은 [`docs/debug-bot-match.md`](docs/debug-bot-match.md)를 참고하세요.
최신 beta.4 변경 파일·검증 결과는
[`docs/v0.5.0-beta.4-report.md`](docs/v0.5.0-beta.4-report.md)에 정리되어 있습니다.


> v0.5.0-beta.14 policy: the Ranked allowlist checks currently enabled + loaded mods only. Installed-but-disabled mods do not block Ranked; required mods such as CBF and Node IDs must still be active.


## alpha.18 attempt scoring reliability
The Ranked client now snapshots the revealed map before entering PlayLayer, retries local attempt registration, buffers progress while start acknowledgement is pending, and only marks an attempt end as reported after it is actually queued. The alpha.17 vanilla song gate remains unchanged.
