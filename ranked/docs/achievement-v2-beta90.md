# Achievement System v2 — beta.90

Later policy update: [beta.91 Debug parity](achievement-debug-beta91.md) enables
Debug achievements like normal matches without resetting v2 data again. The
beta.90 Debug exclusion described below is the original release boundary.

## Release and data boundary

Based on main `528de46950e96ee5c8d970f9fdffea9fa594ba26` (beta.89).
The owner explicitly requested a fresh achievement system and later authorized
main publication after validation. Migration `0027_achievements_v2.sql` resets
**only achievement statistics, Unlocks, AP ledger/total AP and equipped achievement
titles once**. Old result Unlock notices are removed without changing competitive
results. Accounts, sessions, Solo/Survival game history, LP, MMR and Survival
profiles are preserved. The version-2 activation receipt prevents a restart or
migration retry from erasing newly earned v2 data. Admin common resets preserve
this schema/cutover receipt too.

Pre-cutover records are intentionally not backfilled. Existing players are marked
as initialized at the reset; lazy recovery for new accounts only considers
post-cutover authoritative settlements. LP edits, current profile tier, skipped
Placement, Debug Bot, local replay/observer and practice are not performance facts.
Normal Placement completion and normal Solo result predicates remain unchanged.

## Catalog and server authority

The shared rules catalog defines exactly 46 unique IDs:

| Scope | Progressive | One-shot | Total |
|---|---:|---:|---:|
| SOLO_RANKED | 10 | 10 | 20 |
| SURVIVAL | 8 | 10 | 18 |
| SHARED | 3 | 5 | 8 |

Every progressive achievement uses I–V and independent 5/10/20/40/75 AP rewards.
One-shot Common/Rare/Epic/Legendary/Mythic rewards are 10/25/50/100/200 AP.
Internal grade identifiers remain distinct from presentation colors.

Normal Survival settlement path:

`SurvivalOnlineService.settle` → immutable personal LP result → terminal roster
refresh transaction → `SurvivalService.recordAchievementsWithin` →
`AchievementService.recordSurvivalResult` → stored FINAL_RESULT snapshot + paid
real-player result → `survivalAchievementFacts` → unique event and compact match
facts → completion-ordered metrics/streaks → progressive and one-shot Unlocks →
result `achievementUnlocks` → client result notification.

Facts use actual participant counts, server ranks/qualified flags, real cutoff
boundary rank, accepted terminal Attempts, last decisive Final/Death Match scores,
and the uniquely lowest final seed in the last qualifier. Missing evidence is not
replaced with client percentages. Ties/replayed finals do not duplicate match
awards. Snapshot/settlement/event/facts persistence makes retry/restart recovery
idempotent. Late match facts recompute Survival streaks in completion order.

Shared matches/wins/mastery sum distinct Solo and Survival metrics. Each mode's
GOLD history is recorded only by valid settlement. Dual mastery requires actual
mode-specific progressive-V metrics; an admin Unlock does not invent wins.

## Claim All and titles

`POST /api/ranked/achievements/claim-all` accepts `SOLO_RANKED`, `SURVIVAL`, `SHARED`
or `ALL`. SessionGuard provides player identity; unknown scopes or extra identity
fields are rejected. The game sends only its selected tab's scope.

Single Claim and Claim All share profile-first locking, selected Unlock row locks,
one transaction, ledger uniqueness/`ON CONFLICT DO NOTHING`, claimed timestamps,
and total-AP reconciliation from the ledger. Stored reward snapshots determine
payment. Zero pending, concurrent single/bulk calls, retries and rollback are
covered. Claim All returns grouped IDs/levels, paid amount/count, total AP,
remaining count and server time. UI refetches authoritative cards after success
and shows one aggregate receipt, without replaying Unlock notices.

Titles are authorized from Unlock ownership independently of Claim. All earned
progressive versions remain selectable; one equipped title is validated on the
server. Locked cards/title controls stay disabled and dark gray.

## Presentation

I/Common `#B2BAC9`, II/Rare `#57BBFF`, III/Epic `#C679FF`, IV/Legendary `#FFB046`.
Locked: `#5C6474`. Legendary uses a narrow champagne highlight moving across the
text every two seconds. Mythic uses the exact seven requested pastel colors,
continuously interpolated within glyphs and looping every three seconds.

`AchievementEffects` uses one GLES2-compatible program per animated target and a
target-owned Cocos action. Texture alpha and black glyph outlines are multiplied,
not replaced. No node is allocated per frame, no server animation state is stored,
and subtree cleanup stops its actions. A failed shader compilation uses a safe
smooth color fallback. Static/locked tiers create no animation. Card headings,
rarity emphasis, title selectors, existing equipped-title displays and result
notices share the same style. Claim All derives its highest tier from each actual
unclaimed Unlock in the selected tab, never the highest already-claimed stage.

The two-column grid and top summary/Claim All controls adapt to phone, iPad and
wide logical viewports. The four active bilingual font weights include all new
Korean glyphs; the reserved Latin font policy and package atlas budget remain.

## Survival follow-up fixes

A round reuses seven cached preview IDs while the reel has eight fixed slots.
Previously slot eight remained ID zero and drew no image. Empty physical slots
now reuse an existing cached preview without changing reveal identity, membership,
network policy or official selected map. A native play-icon placeholder covers
late/missing textures, including the authoritative winner thumbnail.

Direct cleared-map replay and observer entry now remember their native level.
A manual Quit from that exact live PlayLayer requests the existing scene-owned
return before native teardown. Finishing that return releases local Free Play
ownership, allowing repeated re-entry while the server round remains valid.
Ordinary menu Free Play retains its existing round-change return policy. No local
replay submits official Attempts, score, achievement events or settlement.

## Verification and deployment

Local evidence includes catalog/rule tests, authenticated HTTP/PGlite tests,
PostgreSQL concurrency/recovery, native behavior harnesses, SDK builds and actual
surfaceless-EGL shader rendering. CI runs all four platforms with Debug ON/OFF,
Release packaging, font/asset validation and the shader preview renderer. Results
must be reported from actual completed runs, not the existence of a workflow.

Main uses existing Railway auto-deploy and immutable R2 Release/Debug workflows.
The existing publication guard retains beta.89 admission until beta.90 public
packages are available. Never overwrite beta.89 objects, trigger a duplicate
Railway deployment or reset LP/MMR. Verify both public package hashes/channels,
Railway commit and `/health`, `/ready`, `/api/ranked/config` after publication.

Physical iPadOS/Windows GD rendering, real touch/drag behavior, GPU fallback and
native scene teardown still need device QA even when SDK/CI tests succeed.
