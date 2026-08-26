# Ranked 웹 순위 API 배포

이 패치는 Corum-Ranked 서버에 공개 읽기 전용 엔드포인트를 추가합니다.

```text
GET /api/ranked/leaderboard
```

## 적용

1. 패치 ZIP을 `Corum-Ranked` 저장소 최상위에서 덮어씁니다.
2. `ranked` 폴더에서 `npm ci`, `npm run typecheck`, `npm test`를 실행합니다.
3. 서버를 재배포합니다.
4. Render 환경변수에 아래 origin을 추가한 뒤 다시 배포합니다.

```text
CORS_ORIGINS=https://ybaf100.github.io
```

이미 다른 origin이 있으면 쉼표로 구분합니다. 예:

```text
CORS_ORIGINS=https://ybaf100.github.io,https://example.com
```

API는 Ranked 서버 열림 상태와 플레이어 이름, 표시 티어/LP, 배치·승급전 상태,
강등 경고, 승/패만 반환합니다. Hidden MMR은 동률 정렬에만 서버 내부에서 사용하고
응답에는 포함하지 않습니다. 인증 토큰도 반환하지 않습니다.

정렬 규칙은 아래와 같습니다.

- Ranked: 높은 티어 우선. 같은 티어의 승급전 플레이어는 승급전 승리 수 → 승률 →
  Hidden MMR → 이름순이며, 일반 플레이어는 LP → 승률 → Hidden MMR → 이름순입니다.
- Unranked: Ranked 목록 뒤에 별도 분류하며 승률 → Hidden MMR → 이름순입니다.
