# Achievement Debug parity — beta.91

This follows the owner's later instruction that Debug matches earn achievements
exactly like ordinary matches. It supersedes beta.90's Debug-excluded policy.

Solo `RANKED_PVP` and `DEBUG_BOT` settlement use the same match-fact evaluator,
progression, shared totals, AP ledger, Claim/Claim All and title validation. A
real human account receives the awards; simulated/persistent bot identities do
not become claimable player accounts. Force Win/Lose uses its actual server
result; it does not fabricate Clears, PERFECT WIN or other missing facts.

Survival Debug stores its paid result, then calls the same achievement settlement
inside that result transaction. Only the authenticated Debug owner and canonical
`sur_debug_matches` row can identify the Debug source. It uses the same finished
snapshot, real round/Attempt evidence, rank/cutoff/seed predicates and immutable
match event/facts keys as Online. Result notices include these Unlocks, while
repeated views, worker recovery and duplicate final settlement do not increment
metrics or AP twice. Early elimination waits for terminal match facts.

Migration `0028_debug_achievement_parity.sql` adds a one-time `debug_enabled_at`
policy timestamp to the existing v2 receipt. **It does not clear any v2 records,
Unlocks, AP, titles, LP or MMR.** Debug results completed after this release's
activation qualify; older Debug-excluded results are not retroactively appended
to existing streaks. Normal result eligibility keeps the v2 activation boundary.
A migration retry preserves the same timestamp. Recovery replays only eligible
post-boundary canonical results in each mode.

Practice, observer and cleared-map local replay never submit official results and
remain excluded. The normal-mode timing, Pause/entry flows, LP/MMR, matchmaking,
client Debug build exclusion and the 46-achievement catalog do not change.

Verification covers failing-before/passing-after Solo FORCE_WIN/FORCE_LOSE,
Survival Debug wins/GOLD/AP/title, repeated final views/worker calls, Claim All,
trusted-source validation, policy timestamp/idempotence and preservation of v2
AP/title state. All-platform Release/Debug builds, actual native regressions and
normal Online PostgreSQL checks still apply to the versioned packages.
