# v0.5.0-beta.34 — automatic round READY before resource timeout

Starting main: `108b8ff2db73b2823e395ca99b43c1982fc8d6a5` (beta.33).

The supplied iPad log `Geode 2026-10-07 11.07.03.log` shows native map
acquisition completing in three matches. Blast Processed completes at 11:08:08,
Clubstep at 11:11:11, and Theory of Everyth 2 at 11:12:57. The first match stays
in ROUND_PREPARE and is cancelled; the second reaches MATCH_RESULT without a
ROUND_PLAYING transition. The third capture ends after the successful map
callback. The screenshot reports map/audio ready while the footer waits for
the local download. Neither evidence supports a failed map download diagnosis.

Beta.33 incorrectly computed preparation elapsed as `10 - remainingDeadline`.
The server's resource budget is at least **60 seconds**, so READY eligibility
was postponed until the expiry boundary. The server checks timeout before
processing READY, and Round 1 timeout cancels the match with
`ROUND_1_MAP_DOWNLOAD_TIMEOUT`. The cancellation reason is consistent with the
observed flow; the device log does not itself print its server reason field.

Use the independent monotonic phase countdown for the existing ten-second
Ranked / five-second Placement READY gate. Native map, song metadata, music and
SFX acquisition starts automatically and requires no download button clicks.
Once resources are complete and the countdown has elapsed, READY retries at
bounded one-second cadence until acknowledged. Only server-authorized gameplay
can launch. Do not shorten the server safety budget or change scoring rules.
An already prepared local side now displays SENDING READY instead of falsely
claiming its download is unfinished. Emit a bounded auto-ready request log for
the next device capture.

Regression executes the actual popup updateAutomation method together with
the complete native preparation adapter, without pressing download buttons:
incomplete audio blocks READY; complete map/music/SFX at ten seconds sends READY
with fifty seconds still left on the server resource deadline; delayed ACK
retries are bounded; ACK suppresses retry; Placement uses five seconds. The
same fixture builds and executes under Windows and iOS platform flags.

Client, workspace, lock metadata and server exact client gate are beta.34.
Spectator protocol v2 / Debug recording format v2, native hook inventory,
Placement final-attempt lifecycle and spectator Safe Mode remain unchanged.

**Not device verified.** The submitted beta.33 log demonstrates the failure,
not the beta.34 fix. Real iPad acceptance still requires a new match with no
manual download input, automatic READY, server PLAYING and native scene entry.
