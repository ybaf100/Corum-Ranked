# v0.5.0-beta.35 — native entry, surrender and round UI

Starting remote main: `b2c10aa6b8e7668583d399bd4c097794a4ea5601` (beta.34).
Spectator protocol v2 and Debug recording format v2 are unchanged. No PlayerObject
detours were added and the removed `setVisible` / `saveNewScore` hooks remain absent.

## Evidence and fixes

- The supplied `Geode 2026-10-07 11.34.29.log` reaches ROUND_PLAYING and an accepted
  Attempt Start. Round 1 enters SETTLING at 11:40:09, its last End is ACKed at
  11:40:10, but the child gameplay scene returns only at ROUND_RESULT at 11:40:36.
  The log does not prove the server Spectator toggle value. The new scene-owned
  handoff waits for the genuine final Attempt and its End ACK, immediately returns
  to the retained percentage screen when Live Spectator is OFF, and bounds initial
  eligibility waiting to five seconds when ON. Existing attached viewers retain
  their normal waiting/reconnect lifecycle.
- Surrender's pending payload used `matjson::Value::isObject()` as a request flag.
  Geode 5.10.1's matjson 3.3.1 default constructor creates an object, so the first
  press was treated as already pending. An explicit optional payload fixes the
  first press and its reset. Completed Attempt delivery is drained before
  concession; retries keep the exact event and round context, and stale callbacks
  cannot concede a later round. Server scoring/concession rules are preserved.
- Pause shows a native green circle/white flag in the existing center-button row.
  Round / Match / Cancel and confirmation use Corum's own neon surfaces/fonts,
  English/Korean labels, and bounded send/retry/error status. Match-wide 30s budget
  persists across rounds. The modal closes before budget expiry resumes play.
  Ranked manual exit is disabled and guarded; automatic PlayLayer return, ordinary
  gameplay, Placement and spectator exits retain their existing authority.
- Resource READY no longer starts the gameplay clock. ROUND_ENTERING and
  DEATHMATCH_ENTERING persist an exact map/round/DM entry certificate. Only a
  running canonical PlayLayer sends authenticated `/entered`; `pushScene` alone
  cannot ACK entry. Both players must enter, then the server publishes `startsAt`
  three seconds ahead. A bot admits its synthetic side only after human entry.
  Missing entry has a bounded 60s timeout using the existing cancellation/forfeit
  policy. Recovery preserves the countdown and exact server clock origin.
- While waiting/counting down, the existing native world-frame boundary stops
  gameplay, music, input and Attempt evidence. UILayer touch/keyboard/keypad/Pause
  input is disabled and restored with its previous values; held input is cleared
  at release. Deferred vanilla attempt bookkeeping runs once at GO. The Black
  3/2/1 shrinks into view, fades in, holds and fades out on the shared server
  timeline. A bounded exact-context start certificate avoids an extra PLAYING
  polling round trip. Deathmatch keeps its three-attempt cap, raw percentage,
  publisher identity and no Round intent even before that next state poll.
- Round/Deathmatch/Placement map/music buttons are passive Downloading/Ready/Failed
  displays with progress. Acquisition and bounded retries remain automatic; LDM
  and Disable Shake options remain. The initial mod asset-pack download gate is
  separate. Title and demotion/promotion/placement status have separate rows.
  New labels use Corum fonts, with the required Korean glyphs in all used weights.

## Percentage screen reference

Connected Notion **Corum Ranked — Client / UI / UX**, 2026-09-11 section, describes
the fallback as opponent name, live percentage and score comparison, and defines
Finished as no further legal play rather than merely the previous Attempt ending.
The existing RankedPopup percentage projection is retained; the native scene is
returned to it through the scoped round-return path.

## Server rollout

Deploy the beta.35 server/client together. Migration `0013_native_scene_entry.sql`
adds `entry_state` and the two ENTERING states to DB checks. DatabaseService's
required-migration list and schema readiness probe include it; startup applies
the migration before serving. Already-playing rounds with no certificate continue
their existing lifecycle. The exact client version gate rejects beta.34.

## Validation and remaining device QA

Executable native doubles cover the real transport, entry gate, countdown UI,
pause/modal callbacks, first-click object-default surrender, terminal handoff,
DM Start/End/three-attempt cap and stale callbacks. Existing Placement final-clear,
persistence, Spider boundary, World Driver, camera and Safe Mode suites remain.
Strict C++ suites, workspace tests/typecheck/build and platform SDK CI are required.
CI success is not actual iPadOS verification.

On the same iPad/mod stack, verify both humans waiting/GO and bot GO; English/Korean
flag/modal, timeout resume and 30s across rounds; disabled manual exit; timer=0's
alive final Attempt, then death/Clear and OFF/ON handoff; automatic map/all-song/SFX
download without buttons; LDM/shake; equipped title plus demotion warning; normal
Practice stability; Debug RECORD/TEST, repeated Spider, camera/world and safety.
Existing clock synchronization uses server response receipt and still has RTT
bias; actual two-device start skew requires measurement. Full trigger/world parity,
Dual/cosmetics/reconnect polish and persistent transport remain Phase 5/6 work.
