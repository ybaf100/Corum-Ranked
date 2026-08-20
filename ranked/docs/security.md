# Corum Ranked 보안 경계

## 보장하는 범위

- 모든 Match 상태 전이와 MMR 반영은 PostgreSQL transaction 안에서 수행합니다.
- 서버가 config/maps를 Match 단위로 snapshot하고 서버 시각으로 attempt start 유효성을 판정합니다.
- session token은 서버 secret으로 hash/검증하며 Match에는 별도 scoped token을 사용합니다.
- allowlist는 설치된 비활성 `.geode`까지 검사하고 internal/system 항목만 제외합니다.
- CBF는 설치·enabled/loaded·최소 버전·필수 설정을 session, queue join, Ready에서 재검사합니다.
- Discord destination은 공식 HTTPS webhook host로 제한하고 `allowed_mentions`를 끕니다.

## 보장하지 않는 범위

Geometry Dash account ID/username은 현재 공식 cryptographic login proof가 없는 self-asserted 값입니다. 응답에도 `SELF_ASSERTED_GD_ACCOUNT_WITH_SERVER_SESSION`으로 명시합니다. Geode client의 installed-mod snapshot과 progress/clear 보고 역시 변조된 native client나 외부 injector까지 증명하지 못합니다.

따라서 이 버전의 anti-cheat 범위는 명세대로 **허용된 Geode 환경 강제 + 서버 권위 판정**입니다. 강한 신원/실행 무결성이 필요하면 별도의 account challenge, replay/telemetry 검증, server-side anomaly detection, 운영 moderation을 추가해야 합니다. 현재 구현을 완전한 anti-cheat나 Geometry Dash 공식 인증으로 표현하면 안 됩니다.

관전 overlay의 `currentProgress`는 특히 신뢰하지 않는 임시 telemetry입니다. 서버는 활성 attempt에만 값을 연결하고 허용된 viewer에게만 보여주지만, 해당 숫자를 awarded score, Clear 승인, LAST ATTEMPT 결과 또는 MMR 계산 입력으로 사용하지 않습니다.

## Secret 정책

- 실제 `RANKED_SESSION_TOKEN_SECRET`, DB password, Discord webhook은 source, `.env`, fixture, ZIP에 포함하지 않습니다.
- `.env.example`에는 placeholder 또는 빈 값만 둡니다.
- Debug Bot password는 query string이나 로그에 넣지 않고 request body에서만 검증합니다. Production에는 debug route와 password를 배포하지 않습니다.
- Geode의 Ranked 서버 URL은 public endpoint이므로 secret이 아니며 session/match token은 메모리에만 둡니다.
- 로그에는 bearer token, match token, webhook URL을 기록하지 않습니다.
