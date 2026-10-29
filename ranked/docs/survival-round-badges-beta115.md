# Survival round badges (beta.115)

## Authority and permanence

`survival-round-badges.ts` resolves server-confirmed results. Elimination overrides rank; surviving ranks 1/2/3/4–5/other receive MYTHIC/LEGENDARY/EPIC/FEATURED/RATED. Tied ranks and qualification come directly from the existing reducer. No LP/AP calculation changes or additional rewards.

The engine seals `roundBadges` into its existing match snapshot, and inserts `sur_round_badges` in the same transaction as round projection. The append-only primary key is `(match_id, participant_id, logical_round_key)` with an additional human account uniqueness index. Bots have NULL player IDs and never enter a user's collection. Cancellation excludes all collection counts. Replay, spectator and Free Play have no award write port.

Preliminary rounds each have their own result. FINAL and all TIEBREAKs share logical `FINAL`. A tied qualifier waits for its own decisive outcome; the decisive round's map/difficulty is recorded. Thus a Final loser receives ELIMINATED, and a winner of repeated Death Matches receives one MYTHIC Final badge. A surrender with a server-fixed rank/confirmed elimination time may be recorded before the whole round finishes. Unreached rounds produce nothing. R1…R4 labels follow actual ordinal, regardless of the internal starting phase.

Startup backfill reads completed historical snapshots in bounded pages, uses this same resolver, and inserts with ON CONFLICT DO NOTHING. Missing rank/map/difficulty evidence is skipped. Existing snapshots and attempts are not rewritten. History reads the immutable ledger; live results read sealed snapshot awards. Ordinary RESET_HISTORY visibility changes do not affect collection statistics. The existing explicit administrator RESET_DB retains its destructive meaning and cascading result deletion; this release never invokes it in production.

## Difficulty policy and collection API

`survivalCollectibleDifficulties()` derives the union of ALL tier groups and ALL phases from the same `RANGES` used by official selection. It never reads candidate inventory, earnings or a single current tier. At this version the union is Hard, Harder, Insane, Easy Demon, Medium Demon, Hard Demon and Insane Demon. Easy, Normal, Auto and Extreme Demon are absent. A policy change changes both selection and gallery membership. Feature grades remain a separate concept.

GET `/api/ranked/survival/badges` requires the existing SessionGuard, returns only the session account's collection, and accepts no award input. It returns ordered policy difficulties even at zero counts, grade totals and eliminated counts per recorded difficulty. Retired difficulties remain represented in elimination details. No client-side history visibility filter changes earned totals.

## Presentation

The existing achievement page gains a BADGES button. A clipped, scaled five-slot strip supports mouse/touch dragging, with an explicit movement threshold before a tap opens details. ELIMINATED is appended last independently of policy. Details use the same colored cards as results/history; failed/malformed requests display an error and retry control, not fabricated zero counts. Session token + generation + weak owner fence asynchronous responses; nested details disable the underlying gesture owner.

The personal result preserves its avatar/rank/LP information on the left and adds only participated badge cards on the right, stamped sequentially with a short overshoot/opacity envelope. Animation is computed from the owned panel's clock, with no delayed callbacks or BGM changes. History adds each earned badge beside its thumbnail and keeps it visible with thumbnails disabled; map feature decorations remain separate.

Result presentation progress is scoped to account + match and saved after completed screens. Podium recipients and a native viewer still watching when Final/Death Match ends see Ceremony → LP animation → Personal. Other players see LP → Personal. A settled early elimination can open its personal result without waiting for the shared game. The ceremony Continue button advances presentation only; existing acknowledgement remains the final close action. Payment remains server-idempotent and independent of presentation.

## Validation limits

Automated rule, PostgreSQL integration, native policy/layout and package checks are recorded in the completion report. Universal macOS is added to the existing Windows/iOS/Android matrices and the immutable publication barrier. The Lightsail job retains fail-closed package gating with a bounded 40-minute channel wait and 60-minute job budget, accommodating the additional macOS Release/Debug ON/OFF builds. A successful compile/package check is not physical GD gameplay, audio or touch verification. No GD 2.2081 application/device is available in this workspace; actual visual/gesture behavior requires a real device check.
