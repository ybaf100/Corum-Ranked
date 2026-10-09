# beta50 UI, Admin and session handoff

Base: remote production main `c8dc3c48fa4fbc795b0a643e411d792751edc864` (beta49).

## Result presentation

- Endpoint badges remain fixed. Tier names sit outside the rounded bar at its vertical center. Cutoffs align with bar endpoints; LP alone remains centered on the moving fill endpoint, with the delta as a separate suffix.
- Bar height is 16, with an 8-unit cap radius. Remove the vertical end markers that obscured the rounded silhouette. Move badges 9 units closer to the LP row.
- Track color is the immediately lower displayed tier: RED gray, AQUA RED, BRONZE AQUA, SILVER BRONZE, GOLD SILVER. Make it opaque and wipe changed colors from left to right with a 0.55-second eased ramp. Keep the established 2/4-second LP durations and continuous sub-LP interpolation.
- Skip/Esc before completion shows the final LP, fully assembled endpoint icons and every pending notice; it stays open. Natural completion waits for notice stamps to finish. In both cases the caption becomes Continue/다음. The next press closes through the existing deferred retirement/strong-reference path.
- Demotion warnings stamp into view. Add missing Korean glyphs, including 티 in “AQUA 티어로 승급했습니다!”. Placement keeps its established result/reveal flow; promotion/demotion reveals still hold until Continue, or explicitly Skip/Esc into the score overlay.

## Lobby and records

- Use one streak/promotion/warning row, falling back to YOUR RANK/내 랭크, in the lobby and player cards. Keep the equipped title independent. Standings add the total population beside Top %.
- Increase the lobby emblem from 68 to 74. Put its backdrop under the same motion parent, preserving the halo's own animation. SILVER/GOLD ambient visuals apply to both player cards and a compact badge in INFO.
- Anchor Settings within the viewport and separate language buttons using their scaled widths plus a gap.
- History scrolls through the latest 30 competitive results, with original Detail indices and full text fitting. Keep the separately displayed Placement stage records.

## Debug / Admin

The single Debug lobby entry opens Bot Match, Spectate and Admin. Client Admin entry requires a logged-in GD account named exactly `hwanhee1`. Server mutations independently require the DB-backed session name `hwanhee1` and the existing `DEBUG_BOT_PASSWORD`; no new secret or profile-code step is introduced. The underlying session identity is still self-asserted GD account data, so this implements the requested name/password restriction rather than GD account ownership proof. Admin is disabled when server Debug Bot is disabled.

Admin supports:

- Grant every achievement reward/level, idempotently, with one response summary rather than ordinary unlock notices. AP still requires Claim; actual match metrics are not fabricated. Owned progressive levels remain selectable as titles.
- Set LP and its configured tier, MMR only, or both, targeting a nickname (default hwanhee1). LP is nonnegative; MMR retains its signed integer domain. LP edits complete placement and clear promotion/demotion warning state. Reject ambiguous nicknames and queued/active targets; rating edits also reject an unfinished Placement run.
- Delete all bot profiles and their FK-linked match/queue/outbox records, preserving unrelated human profiles/results.
- Reset the named player's competitive history visibility without deleting the opponent's shared record or resetting LP/MMR/achievements/Placement. New records remain visible. Migration `0016_admin_history_reset.sql` adds the cutoff; server startup applies and probes it.
- Confirm DB reset with a separate native dialog. Clear player/game/session/achievement/forum/debug recording data, keeping operational snapshots, runtime settings, migrations and schema. No Admin action was run on the live DB during development.

Retire owned modal priority registrations on scene exit. Delay destruction/next modal creation until after native input dispatch, cancel retired requests and ignore late responses or changed sessions. Reload authoritative projections after a successful action, retiring pending lobby metadata reads first.

## Other-device takeover

Normal admission still reports `RANKED_ACCOUNT_ACTIVE`. Its popup offers an explicit red Disconnect other device button; simply opening or closing it never takes over.

Only that choice sends `takeOver: true`. Serialize admission on the player row, revoke prior sessions, rotate the player's match-scoped token and move/recreate the existing MATCHED assignment for the replacement. Return `activeMatch` so the client recovers via the normal queue-status/match-state path instead of starting another match. Preserve match state, score and the opponent's token. Keep the shared 90-second lease/5-second heartbeat.

Queue entry creation rechecks the current live session inside the transaction, serialized with admission/Close. Scope queue status/heartbeat/leave to the exact session, preventing a former device's delayed request from overwriting, refreshing or releasing the new assignment. Former credentials cannot heartbeat or access the match; the former client observes revocation at its independent heartbeat. No transport/protocol redesign or native gameplay hook changes.

## Validation

- Server: 256 tests across 38 files passed after the final server changes, including real HTTP SessionGuard/DTO/Admin routes and an isolated migrated PostgreSQL fixture, all Admin mutations and confirmation, history visibility/latest-30, takeover recovery, match-token rotation, and late Close/queue requests.
- Rules: existing 114 tests across 8 files passed; no rule algorithm change. Workspace production/test TypeScript typechecks passed with beta50 metadata.
- C++: 9 existing strict executable suites passed. Corum score/Debug/Admin/conflict popup code executes against native boundary doubles, including Debug ON/OFF, duplicate input, late responses, confirmation, first Skip vs next Continue, icon settlement and intermediate color wipe.
- Mod source/native regression: all 138 tests passed after the final client changes. Image-pack build regression: 5 tests passed. Image-pack inputs are unchanged; conditional image packaging remains in place. Workspace production build also passed with beta50 metadata.

These are local source/host/database checks. GD/Cocos/HTTP boundaries in executable fixtures are doubles, not a GD device. No new device acceptance or full native platform package is claimed. Do not inspect/wait on workflow results for this push, per the user's instruction. No manual Release or force push.
