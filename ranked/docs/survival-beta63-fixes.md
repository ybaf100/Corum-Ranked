# Survival beta63 fixes

Base: remote main `54117ad0b881dfbb1aadf174f1b1e50d14a54fca`, including the user's Debug-button visibility change. M3 and M4 remain integrated in the existing client/server. Version: `0.5.0-beta.63`.

## Results and records

Survival LP animations and history show the final rank rather than Solo WIN/LOSS text. First, second and third use gold, silver and bronze; other positive-LP ranks use green. Any negative actual LP delta takes priority and uses red. Solo result behavior remains separate.

The Survival profile summary derives wins and losses from settled match LP ledger entries, falling back to persisted debug result data. Actual LP increase is a win; unchanged or decreased LP is a loss. The card reuses the Solo wins/losses/win-rate formatter. Existing ranks, LP, records and settlement keys are retained. History resets still obey `history_visible_since`. No counters or migration require backfilling.

Survival history creates one summary label per row. The scroll container can no longer restore an unwanted hidden Solo summary over it. Achievement cards wrap the complete server-provided descriptions into their reserved area instead of truncating them with ellipses. Rank/Top card captions omit their separator slash.

## Map selection and preparation

Read-only reference: [Versus](https://github.com/Rlaxogus123/Versus). The provider applies the native search approach of querying individual difficulty categories and choosing bounded random pages. This patch does not copy its category rules or delegate map authority to clients.

Catalog searches use separate normal-difficulty filters, cap the advertised random search window at 10,000 levels, visit eligible categories in turn and retain validated page-zero fallbacks. Sparse pages and individual stale records do not invalidate every candidate on a page. Download verification still enforces Rated, level ID at least 40,000,000, exact stars/Demon subtype, non-Auto, non-Platformer, valid geometry and match-local reuse restrictions.

Debug match polling coalesces map-selection jobs using the engine's existing durable selection lease and retry time. Responses include `mapSelection.errorCode`, `retryAt` and `selecting`; clients display a localized retry state. Unconfirmed maps never start round timers. No live GD request was made during this patch, so operation against the deployed provider still needs the user's runtime check.

The waiting scene retains participant tiles by ID. Alive players have native cube icons, names above, rank/progress below and eased repositioning. Normal entry and subsequent rounds remain automatic. The old Continue control becomes Re-enter and appears only after that round was entered and the player has returned from it. LDM, Disable Shake and Jukebox remain available; open confirmation/Jukebox dialogs defer automatic entry. Survival surrender requires a confirmation tied to the same match, round and map generation.

## Lobby and gameplay presentation

Matchmaking text/play/leaderboard contents remain hidden during the card crossing. They fade in after movement, with input locked until completion. Back fades contents out before reversing the path. Hover/press feedback is scoped to the Ranked lobby; chooser cards use one quick clockwise highlight with the original frame thickness. Debug and Settings align within the footer panel.

Both leaderboard endpoints accept optional `search` (1–32 characters), prefer an exact case-insensitive player name and otherwise locate the first matching substring. The response preserves page data and adds `search: { query, found, gdAccountId }`; `offset` identifies the containing page. The popup jumps to/highlights that row and rejects stale results after mode changes or close.

Solo surrender text uses the same readable Corum menu fonts and larger multiline body. Official local Demon levels display Demon while community Demon metadata keeps its five subtypes. Death-feed percentages are integers with fade-in; server progress and Death Match precision are unchanged.

## Applying the patch

Deploy the matching beta63 client and existing NestJS server together because the exact version gate is synchronized. Existing migrations through `0023_survival_m4_debug.sql` remain required; there is no new migration, Apps Script redeployment, configuration field or operating DB action in this patch. Public Survival matching remains M5 work.

Per the user's request, no tests, builds, device checks, Android package inspection or GitHub Actions inspection were run for beta63. Existing test boundaries and affected expectations were updated in source, without executing them. The user will validate native layout/animations, map preparation, surrender, search and full debug matches after the push.

### Validation follow-up

After the initial push, the user reported the failed Debug policy job. Read its completed logs (run `38203374167`) and the server validation logs (run `38203373910`). Domain compilation had passed; the native stage reported ten failing cases and the server map-provider suite reported two. Corrected the obsolete description-truncation assertion and missing native hover/footer/history boundary declarations. Removed an accidental Survival-only field from the Solo result-player structure, restored legacy entry-control hiding, and changed the search prompt to supported Corum glyphs. The malformed catalog fixture now uses an actually invalid numeric field; repeat ineligible maps no longer pollute the duplicate-candidate pool. Validation jobs and warning levels remain enabled. No local builds/tests or device checks were run for this follow-up; the newly triggered Actions result is left to the user.
