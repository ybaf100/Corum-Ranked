# Survival beta.87 follow-up

This patch follows remote main `050806051f8fa663ca87b43b02570114812fedce` (beta.86).
It preserves beta.85's result/observer/prefetch state machine, beta.86's reserved
ASCII-only Regular font and 96 MiB atlas budget, Debug policies and immutable packages.

## Targeted corrections

- The in-game rank text remains. Its hand-drawn halo, pulse state and floating
  status-board button are removed in every Survival HUD mode. Boards remain
  accessible from the coordinator, individual results and podium.
- All Survival cards route to one Corum custom player-info popup, not native GD
  ProfilePage. The popup reuses the Solo statistics parser, native GD account
  cache/delegate serialization and Corum visual components. Bots never perform
  GD or forum lookups. A closed popup, changed match/session or mismatched account
  cannot receive a late response or take another delegate's ownership.
- `GET /api/ranked/survival/matches/:matchId/player-info/:participantId` is an
  additive, session-protected read of the existing GDKF/Pointercrate cache. It
  checks match membership, resolves the account from the immutable participant
  roster and accepts no account ID supplied by the client. It does not refresh
  external providers, write scores or change settlement rules.
- The selected-map view keeps native featured/epic/legendary/mythic ornaments
  while replacing only its difficulty-face frame with the verified expression-only
  GD frame. Explicit grade text distinguishes pending/unrated/rated/featured grades
  and updates with metadata revisions. GD star sprite/count and length remain.
  Rebuilding for a changed LDM capability or thumbnail mode invalidates the old
  creator/grade/stars/readiness caches and retires the removed difficulty-node
  pointer before cleanup. An unchanged metadata revision cannot leave the new
  labels empty or access the already-destroyed node.
- Podium rank one now uses the existing `createRankAmbientFx("GOLD", ...)` assets
  and animation. The former drawn circles are gone. Podium tier/LP show actual
  `tierAfter` and the additive `lpAfter` field. Unsettled human LP is pending;
  bot LP is virtual and uses the existing settlement input. Late settlement
  refreshes labels without restarting the podium sequence. No global LP footer
  is added to the 16-player board or transition screens.

## Separate timing gates

The initial server-clock six-second intro is unchanged. It still prepares the
official map and round-specific thumbnail candidates. The reel's reveal identity,
candidate membership and shared thumbnail cache remain unchanged.

Previously the client sent `ASSETS_READY` while the four-second reel was still
running. The server's five-second entry hold therefore elapsed behind the reel,
and the selected panel could appear only briefly before native entry/three-second
GO. A raw server deadline alone could also lose visible seconds to response latency.

Now the coordinator latches selected-panel readiness only when the reveal is
settled and the actual map/audio preparation has succeeded. Only that exact
match/round/map-generation context may submit `ASSETS_READY`. Native entry waits
for both the server's `entryOpensAt` and a full 5,000 ms of selected-panel countdown
after receiving its valid `entryRequestedAt`/`entryOpensAt` authorization. A steady
clock prevents frame quantization or clock corrections from shortening that display.
Recreated UI shares the process-owned deadline rather than restarting the countdown.
This presentation gate may delay entry; it never authorizes early entry or GO.
Faster peers advancing the shared state to COUNTDOWN/PLAYING cannot consume the
remaining visible hold of a participant who has not entered native play yet.
An already-entered player's validated re-entry retains the existing server gate.

The native `ENTERED` ACK and server-authorized three-second `startsAt`/GO are still
separate and unchanged. The five-second panel hold does not replace native GO.

## Regression and physical-device checks

Executable regressions cover missing/late authority, full 5/4/3/2/1 boundaries,
recreated UI, delayed server deadlines, re-entry, custom popup identity/lifetime,
foreign GD delegates, bot isolation, feature grades and paid/pending/virtual LP.
The previous implementation fails the new HUD, profile-route and timing reproductions.
The real SDK build and existing observer, pause, surrender/Force Clear, thumbnail,
round-transition and packaging tests remain part of validation.

CI and native boundary tests are not iPadOS or Windows device tests. Verify on
devices: the full selected-panel five beats followed by separate native GO,
popup touch/close/stacking during live updates, feature ornaments without face
labels, compact podium tier/LP layout and Gold ambient, and no in-game halo/button.
Use existing `[SurvivalReveal]`, `[SurvivalObserver]` and `[RankedEntry]` logs to
correlate selected-map completion with native entry. Do not reset production data
or alter Railway/Neon settings to validate this patch.
