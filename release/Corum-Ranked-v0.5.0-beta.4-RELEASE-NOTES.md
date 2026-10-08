# Corum Ranked v0.5.0-beta.4

## 변경 사항

- 상대의 Hidden MMR 강도를 MMR과 표시 LP에 각각 반영하는 운영 가중치를 추가했습니다. 두 값은 0~2이며 기본값 1은 표준 Elo 강도입니다.
- 강한 상대를 이기면 더 많이 오르고 약한 상대에게 지면 더 많이 내려가며, 반대 상황은 변동이 완화됩니다. 기존 최소/최대 LP 배율은 그대로 최종 한계를 정합니다.
- 무패이며 모든 결정승이 실제 3-Clear `LAST_ATTEMPT_EXPIRED`로 끝난 승자를 `PERFECT WIN`으로 판정합니다. 무승부 라운드는 허용하지만 한 라운드라도 패배하거나 몰수·강제 승리·스코어 승리·Death Match 승리이면 제외됩니다.
- `PERFECT WIN` 승자는 기존 공식의 이론상 최대 Hidden MMR 획득량과 일반 LP 최대 획득량을 받습니다. 패자는 기존 공식을 그대로 사용하며 배치·승급전·강등 상태 규칙도 유지됩니다.
- 결과창의 해당 승자 카드에 노란 `PERFECT WIN`을 표시합니다.
- 게임 HUD 타이머 위에 `ROUND 1~3` 또는 `DEATH MATCH N`을 표시합니다.
- Hidden MMR·LP에 반영되는 전체 요소와 계산 순서, 배치·승급·강등 예외를 `ranked/docs/v0.5.0-beta.4-mmr-lp-policy.md`에 정리했습니다.
- 클라이언트와 서버의 필수 버전을 `v0.5.0-beta.4`로 맞췄습니다.

## 배포

추가 DB migration은 없습니다. 최신 Apps Script를 재배포하고 `setupCorumRankedConfig()`를 한 번 실행한 뒤 새 가중치 두 값이 0~2인지 확인해야 합니다. 이후 beta.4 서버와 정확히 일치하는 beta.4 클라이언트를 함께 배포합니다.
