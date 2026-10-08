# v0.4.0-alpha.11

- Fixed CI regressions caused by old integration-test expectations after the alpha.10 2-Clear LAST ATTEMPT rule change.
- Debug Bot rated-match tests now wait for the authoritative 10-second LAST ATTEMPT window to resolve before the next round is readied.
- Match-flow relay tests now expect the two legitimate LAST_ATTEMPT events produced by the tested two-round flow.
- No production Ranked scoring, rating, download, or allowlist rule was weakened or bypassed.
- Corum Integration remains unchanged.
