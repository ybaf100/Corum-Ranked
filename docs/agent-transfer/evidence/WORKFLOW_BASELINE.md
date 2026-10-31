# GitHub Actions 실제 측정 기준

조회 시점: 2026-10-31 02:10 KST 전후. GitHub Actions REST metadata 기반.
Repository: ybaf100/Corum-Ranked
main SHA: 5875c91cc3bf64b3eb1cfc3c019dcc5ef4f16b68
version: v0.5.0-beta.121

| Commit | Workflow | Run ID | Created UTC | Updated UTC | 경과 | 최종 상태 |
|---|---|---:|---|---|---|---|
| beta.121 | Validate Corum Ranked | 41093892859 | 16:53:06 | 16:58:31 | 5m25s | success |
| beta.121 | Build Corum Ranked Mod | 41093893195 | 16:53:06 | 17:01:21 | 8m15s | success |
| beta.121 | Build Corum Ranked Debug | 41093892958 | 16:53:06 | 17:07:51 | 14m45s | success |
| beta.121 | Deploy Corum Ranked to Lightsail | 41093893227 | 16:53:06 | 17:08:45 | 15m39s | success |
| beta.120 | Deploy Corum Ranked to Lightsail | 41092839172 | 16:45:58 | 17:00:09 | 14m11s | success |

GitHub Actions URLs: `https://github.com/ybaf100/Corum-Ranked/actions/runs/<RUN_ID>`.

코드에서 확인된 주요 병목:
- `.github/workflows/build-ranked-debug.yml`의 build 및 Debug OFF 네이티브 jobs에 `needs: tests`가 있어 테스트 완료 후 플랫폼 빌드 시작.
- Debug 5 플랫폼 ON 빌드와 5 플랫폼 OFF 빌드, 추가 iOS OFF 및 도메인 검증.
- Release 5 플랫폼 빌드 및 combined package + R2.
- Deploy는 독립적으로 server Docker image 빌드 후 `verify-published-clients.py --wait-seconds 2400`로 immutable Release/Debug 게시 대기.
- `concurrency.group: ranked-lightsail-production`, `cancel-in-progress: false`로 운영 배포 직렬화.
- CI에는 `Simulate 1050 adaptive NORMAL matches and a ten-hour virtual soak` (가상 시간 시뮬레이션) 및 PostgreSQL 서버 HTTP 동시성 테스트가 있다.

이는 workflow 총 경과 시간 관측. 특정 step의 실제 병목은 GitHub Actions jobs/steps timestamps와 로그로 재확인한다.

가장 즉각적이며 상대적으로 위험이 낮은 개선은 Debug `tests`와 네이티브 platform build들을 병렬로 바꾸되, 최종 publish 및 deploy까지 필요한 모두를 required gate로 유지하는 것이다.

운영 이미지 빌드 도중의 강제 취소나 클라이언트 검증 제거는 금지한다.
