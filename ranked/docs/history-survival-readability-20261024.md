# Survival / history readability follow-up — 2026-10-24

Base main: `64de7c14643fd6dbe3794ac5baaa94fa245dd71f` (beta.97). This follow-up targets `ybaf100/Corum-Ranked` and is requested for main. It preserves the existing release version and immutable public packages; build-only commits validate the code without republishing a beta.97 object or activating a server update.

## Behavior

- Card density follows the actual survivor count: up to 16 uses the existing full player cards; 17 or more uses nickname/icon cards. The count uses confirmed qualification during elimination presentation, rather than a provisional live cutoff. Tied survivors above 16 keep compact cards. Full-card result/round rosters also paginate when they contain eliminated players or the screen is short.
- Player names remain on one line and shrink to their allocated width/height. Fitting first clears any earlier wrapping width and resets the starting scale, so reused labels can grow again when their name becomes shorter. Original casing is preserved.
- Match list cards separate the result, LP delta, main rank/score, date/mode and round chips. Primary text uses the stronger font and higher contrast. Round details and refresh/settings controls have distinct space. Whole-card tap/drag behavior and the 30-record limit are retained.
- History level panels reuse the native GD difficulty/feature badge and readable title/creator/ID/length/stars/grade rows. The same layout applies to Solo rounds/DM and Survival rounds/final/TIEBREAK. If thumbnails or their dependencies are unavailable, the image frame, placeholder, sprite and thumbnail request context are omitted, and the map panel uses the shorter layout.
- A Ranked expiry while viewing records opens the reconnect confirmation above the history layer. It appears once per rejection, keeps other-device takeover separate, and retires safely on scene exit. Reconnect returns to the existing Ranked session entry flow. Cancelling exposes that flow's retry screen instead of leaving an unusable history screen.
- Player names in Solo list cards and Solo/Survival detail boards open the same custom statistics popup as Survival cards. History responses expose only exact stored identities for visible records, with bots always having no GD account lookup. Survival forum reads use the authenticated match-participant route; Solo uses the existing account statistics cache route. Popup lifetime is bound to the visible history owner and current session, without requiring an active Survival game. Queued taps check the render generation and session before opening; drags do not open a profile. Missing historical tiers remain unavailable.
- Earned IV/Legendary achievement progress uses the gold shader sweep. V/Mythic keeps its seven-pastel flowing gradient; lower stages, locked Legendary bars and Claim All controls remain static. This changes presentation only, with no catalog/AP/schema/reset changes.
- Equipped single achievements display **UNEQUIP / 착용 해제** and clear that exact achievement ID. Progressive achievements retain their stage selector. Same-name achievements in different scopes are never matched by display text.

## Validation

Native coverage executes the production fitter, grid policy, title callback, progress-style function, history map-panel renderer and reconnect-modal function. It covers narrow/wide screens, long and reused labels, the 16/17 boundary, large eliminated rosters, optional thumbnail omission, metadata identity, once-only reconnect, cancel/detached callbacks and single-title removal. Existing tests exercise scrolling/tap ownership, 30-row culling/indexing, retained scene return, profile delegate cleanup, bot identity and server-session changes.

Server projection tests retain incomplete-round redaction and access/visibility policy while validating exact GD identities and bot suppression. The SQL query for Solo already joins both player rows, so adding identity fields does not add queries. No DB migration, production write, rating change or achievement reset is part of this patch.

The software OpenGL ES shader check also renders 120 solid progress-bar frames in addition to the existing 120 masked text frames. It verifies that gold/rainbow bars animate and that the text mask/outline behavior is preserved. These tests and CI compilation are not physical-device visual/touch validation.

Final local and CI results are recorded before main integration below.
