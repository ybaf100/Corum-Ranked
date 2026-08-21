# 무료 alpha 호스팅: Render + Neon

Corum Ranked alpha의 현재 배포 조합은 Render Web Service + Neon PostgreSQL이다.

현재 production Ranked base URL:

```text
https://corum-ranked.onrender.com
```

## 1. Neon DB

1. Neon 프로젝트를 만든다.
2. SQL Editor에서 `ranked/migrations/0001_initial_ranked.sql`을 적용한다.
3. pooled PostgreSQL connection string을 `DATABASE_URL`로 사용한다.
4. DB 비밀번호와 connection string은 저장소에 커밋하지 않는다.

## 2. Render Web Service

GitHub 저장소를 연결한 뒤 다음 값으로 Web Service를 구성한다.

| 항목 | 값 |
| --- | --- |
| Root Directory | `ranked` |
| Build Command | `npm ci --include=dev && npm run build` |
| Start Command | `npm run start --workspace @corum-ranked/server` |
| Health Check Path | `/health` |
| Node | `22.22.0` |

`NODE_ENV=production`은 build 단계에도 적용되므로 TypeScript 빌드에 필요한
`@types/node`, `@types/pg`, `@types/express`가 빠지지 않도록 build command에서
`--include=dev`를 명시한다. `PORT`는 Render가 제공하므로 직접 고정하지 않는다.

환경변수 예시:

```text
NODE_ENV=production
NODE_VERSION=22.22.0
DATABASE_URL=<Neon pooled connection string>
RANKED_CONFIG_URL=<Apps Script /exec?action=ranked_config URL>
RANKED_CONFIG_REFRESH_MS=60000
RANKED_CONFIG_FETCH_TIMEOUT_MS=10000
RANKED_SESSION_TOKEN_SECRET=<32자 이상 임의값>
CORS_ORIGINS=
DISCORD_WEBHOOK_URL=
ENABLE_DEBUG_BOT_MATCH=true
DEBUG_BOT_PASSWORD=2008
```

## 3. 배포 검증

아래 순서로 확인한다.

```text
https://corum-ranked.onrender.com/health
https://corum-ranked.onrender.com/ready
https://corum-ranked.onrender.com/api/ranked/config
```

- `/health`: 프로세스 liveness
- `/ready`: DB + Apps Script config readiness
- `/api/ranked/config`: client가 실제로 소비하는 Ranked rules/CBF/allowlist snapshot

`/ready`에서 `databaseReady`와 `config.ready`가 모두 `true`여야 실제 Ranked 테스트로 넘어간다.

## 4. Geode 모드

alpha.6부터 production base URL은 모드에 기본값으로 포함된다. Geode의 string setting을
직접 편집할 수 없는 환경에서도 저장된 값이 비어 있으면 런타임이 자동으로
`https://corum-ranked.onrender.com`을 사용한다.

Debug Bot Match를 포함하려면 CMake configure 시 `-DCORUM_RANKED_DEBUG_BOT_MATCH=ON`을
사용하고 서버에도 `ENABLE_DEBUG_BOT_MATCH=true`를 설정한다. 현재 alpha 정책에서는 Bot Match
결과도 일반 Ranked와 동일한 rating/placement/statistics 경로에 반영된다.
