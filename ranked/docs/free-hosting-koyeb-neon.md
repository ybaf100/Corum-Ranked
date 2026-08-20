# 무료 alpha 호스팅: Koyeb + Neon

2026-08-20 기준 무료 alpha/개발 테스트 조합은 다음을 권장한다.

- Ranked API: [Koyeb Free Instance](https://www.koyeb.com/docs/reference/instances)
- PostgreSQL: [Neon Free plan](https://neon.com/docs/introduction/plans)

Koyeb 무료 인스턴스는 512MB RAM, 0.1 vCPU, 2GB SSD, 조직당 1개이며 1시간 동안
트래픽이 없으면 scale-to-zero된다. Neon Free는 프로젝트당 월 100 CU-hours와 0.5GB
스토리지를 제공하며 5분 미사용 시 compute가 suspend된다. 둘 다 정식 운영 SLA 용도가
아니므로 현재 alpha와 테스트 계정에만 사용한다. 첫 요청의 cold start와 낮은 CPU 때문에
실제 이용자가 늘면 유료 상시 서버로 이전해야 한다.

## 1. Neon DB

1. Neon에서 새 프로젝트를 만든다.
2. SQL Editor에서 `ranked/migrations/0001_initial_ranked.sql` 전체를 한 번 실행한다.
   이 저장소가 최초 배포 기준이므로 이 파일 하나가 완전한 최신 schema다.
3. Dashboard의 **Connect**에서 pooled connection string을 복사한다. Neon도 일반적으로
   pooled 연결을 권장한다: [Neon connection guidance](https://neon.com/docs/connect/choose-connection).
4. 이 문자열은 GitHub나 `.env` 파일에 커밋하지 않고 Koyeb Secret/환경변수
   `DATABASE_URL`에만 저장한다.

## 2. GitHub에 전체 소스 업로드

전달 ZIP은 상위 wrapper 폴더가 없다. 빈 로컬 저장소 루트에서 압축을 풀면
`.github`, `apps-script`, `corum-ranked-mod`, `ranked` 등이 바로 놓인다. 전체를 GitHub에
올리되 `.env`, DB 비밀번호, session secret, Discord webhook은 커밋하지 않는다.

## 3. Koyeb Web Service

Koyeb에서 GitHub 저장소를 연결하고 Web Service를 만든다.

| 항목 | 값 |
| --- | --- |
| Project/Work directory | `ranked` |
| Build command | `npm ci && npm run build` |
| Run command | `npm run start --workspace @corum-ranked/server` |
| Port | `8000` |
| HTTP health path | `/health` |
| Instance | `Free` |

Koyeb는 Git 기반 Node 앱 배포와 custom build command를 지원하며 HTTP health check를
설정할 수 있다: [Git build](https://www.koyeb.com/docs/build-and-deploy/build-from-git),
[health checks](https://www.koyeb.com/docs/run-and-scale/health-checks).

환경변수는 다음과 같이 설정한다.

```text
NODE_ENV=production
PORT=8000
DATABASE_URL=<Neon pooled connection string; Koyeb secret>
RANKED_CONFIG_URL=<Apps Script /exec?action=ranked_config URL>
RANKED_CONFIG_REFRESH_MS=60000
RANKED_CONFIG_FETCH_TIMEOUT_MS=10000
RANKED_SESSION_TOKEN_SECRET=<32자 이상 임의값; Koyeb secret에서 직접 생성/저장>
CORS_ORIGINS=
DISCORD_WEBHOOK_URL=

# alpha Debug Bot Match를 쓸 때만
ENABLE_DEBUG_BOT_MATCH=true
DEBUG_BOT_PASSWORD=2008
```

Discord를 실제 테스트할 때만 `.env.example`의 나머지 Discord 설정과 webhook secret을
Koyeb에 추가한다. Bot Match의 Discord 기본 선택은 OFF다.

배포 후 다음 세 주소를 확인한다.

```text
https://<service>.koyeb.app/health
https://<service>.koyeb.app/ready
https://<service>.koyeb.app/api/ranked/config
```

`/health`는 프로세스 생존, `/ready`는 DB와 검증된 Apps Script config 준비 상태다.
`/ready`가 `ready: false`이면 Spreadsheet 운영값 또는 DB 연결부터 고친다.

## 4. Geode 모드

`hwanhee1.corum_ranked` 설정의 `Ranked server URL`에
`https://<service>.koyeb.app`을 입력한다. 끝의 `/`는 붙이지 않는다. 현재 alpha에서
Debug Bot 버튼을 포함하려면 CMake configure 시
`-DCORUM_RANKED_DEBUG_BOT_MATCH=ON`을 사용한다.

## 무료 플랜 주의점

- Koyeb는 1시간 무트래픽 후 0으로 내려가므로 첫 연결이 늦을 수 있다.
- Neon Free는 5분 무쿼리 후 suspend가 강제된다:
  [Scale to Zero](https://neon.com/docs/guides/scale-to-zero-guide).
- active match 중에는 polling/DB query가 계속 발생하지만, 무료 CPU/DB 한도를 넘는 동시
  사용자는 전제로 하지 않는다.
- 테스트용 계정과 DB만 사용한다. Debug Bot 결과도 실제 rating에 반영된다.
