# Survival round scheduling and forced Death Match — beta.111

Baseline: remote main `8e83e1984de04d758a9be20f9bd8a78346657b0f` (beta.110).

## Confirmed defects and policy change

The online/variable-size path scaled each qualifier target by the original population. It selected FINAL only at <=3, the last qualifier at <8, and ROUND_1 otherwise, so the configured 16/64-player phase boundaries were not respected. Next-round selection always advanced one phase, regardless of actual survivors. Live Death Match numbers were fixed at 4/5, history/Pause labels mapped internal phase directly to R1/R2/etc., and the standings dialog exposed the raw phase.

The authoritative reducer now uses fixed internal targets: 16-player format 10/6/3; 64-player format 40/26/12/5. Initial admission chooses the first phase whose target is strictly below the actual population, otherwise FINAL. Human and bot entrants count equally. The same policy applies to normal, Debug and bot-fill matches. Two entrants start FINAL by default.

At result-to-next-round creation, qualified non-forfeited survivors determine the next phase. Search is restricted to phases after the phase just completed. Ties can increase the field without replaying an earlier phase. A single survivor uses the existing winner path. A FINAL tie still enters TIEBREAK; population alone never enters TIEBREAK.

| Format | Entrants | Internal first phase | Displayed schedule |
| --- | --- | --- | --- |
| 16 | 2–3 | FINAL | FINAL |
| 16 | 4–6 | ROUND_3 | R1, FINAL |
| 16 | 7–10 | ROUND_2 | R1, R2, FINAL |
| 16 | 11–16 | ROUND_1 | R1, R2, R3, FINAL |
| 64 | 2–5 | FINAL | FINAL |
| 64 | 6–12 | ROUND_4 | R1, FINAL |
| 64 | 13–26 | ROUND_3 | R1, R2, FINAL |
| 64 | 27–40 | ROUND_2 | R1, R2, R3, FINAL |
| 64 | 41–64 | ROUND_1 | R1, R2, R3, R4, FINAL |

## Identity and presentation

Stored `SurvivalRound.ordinal` remains the actual created-round order and is projected as `displayRoundNumber` (and the compatible `roundNumber`). Internal phase is unchanged and remains the map-range/scoring/attempt-limit authority. Native preparation, selection, HUD, standings, Pause, spectator, Free Play menu clock and history use server display order. FINAL and DEATH MATCH have named labels, including standalone and repeated Death Matches. Solo labels are unchanged.

Map jobs resolve the confirmed next phase after results. A prefetched map must match phase, round key and generation before adoption. A stale consumed pool candidate is scoped to its exact match/round/generation/level and moved to bounded COOLDOWN; it cannot leak into the new phase. Existing recent-map history is retained.

## Force Death Match

The Debug creation and Fill With Bots popups each own a default-false checkbox. Changing bot counts or difficulty does not reset it; reopening the popup resets it. Only Debug builds with a valid session and Debug password can use these APIs. Normal queue admission rejects the extra field, and trusted non-debug engine admission rejects forced mode. Both DTOs strictly validate the optional boolean.

The mode is stored in the match JSON snapshot. Creation hashes and the bot-fill request ledger include the option. Exact retries reuse the match; changed options with the same request ID conflict. A matched fill cannot be silently upgraded by a new request ID either.

Forced matches create TIEBREAK immediately for any valid population up to the captured capacity. Existing three-attempt, millipercent, 180-second, terminal and top-tie adjudication rules are reused. Repeated Death Match contains only the tied leaders; earlier losers remain in their original elimination stage and rank. Existing LP/achievement settlement and replay Attempt identity use the same authoritative paths.

## Database compatibility

Additive migration `0033_survival_force_deathmatch.sql` drops only the NOT NULL constraint on `sur_tiebreaks.final_round_key` and adds a default-false boolean to the fill request ledger. The original foreign key remains: non-null links must still reference the real FINAL row; null means the match had no FINAL. Existing rows, accounts, LP, AP, attempts and migration history are preserved. Readiness requires the new migration and both schema conditions.

The migration was applied twice on the isolated Neon PostgreSQL 18 branch, preserving its account/LP rows. Production migration is performed by the existing protected Lightsail startup workflow, after both client packages have been verified.

## Verification and limits

Tests exercise the production reducer, service methods, HTTP controllers/DTO validation, persistence/projection, client transport callbacks and display helpers. Boundary schedules cover online and non-online creation in both formats, survivor reduction, ties, one-survivor completion, stale map phase rejection, forced populations 2/3/4/8/16/32/64, three-attempt precision, repeated Death Match, authenticated admission, payload conflicts, snapshot recovery, FINAL-linked compatibility, replay identity and one-time LP settlement.

Full platform packaging remains a separate Release/Debug/Debug-OFF CI stage. Physical Geometry Dash / iPadOS / Android / Windows touch/rendering and in-game play have not been exercised in this environment; automated policy/transport tests and compiled packages do not prove device behavior. No production matches or fake player sessions are created for verification.
