# beta.120: replay clock and terminal recovery investigation

Start main da02467993d1edf369d18dbf3f4bc2e31532d82d (beta.119).
ZIP SHA256 6957f0655e9b8dc21d79bef0f86028cab66195569a9835522cd18a1c6a8a9a55.
All 12 checksummed members verified. Raw user logs remain outside the repository.

| Evidence | Actual code boundary | Conclusion |
|---|---|---|
| Latest iOS/CBF1.5 log lines1239–1241, same Attempt8a87f27f, frame0, tick0: all physics fields equal except timestamp70163.98234975 versus70189.84794037501,70193.55499645833,70197.469151875 | Initial restore writes timestamp0; Native GJ update changes it before the end-of-frame evidence. CBF source does not assign GJ m_timestamp. | Confirmed recording-session versus replay-session clock domains; exact proprietary Native clock instruction is unavailable here. Restore an explicitly recorded input-clock boundary, not poses or an arbitrary offset. Instrument entry/delta/step/exit. |
| Same failures report firstDifferentField=$.visible | completeFrame compares physics but reject receives the whole pose | Confirmed diagnostic compares a different object than the failed predicate. Physics diagnostics must compare physics only; presentation differences remain separate. |
| observe(context change) clears m_attempt, then discardObsoleteEvents removes old-round END; endAttempt rejects changed context | SurvivalRuntime.cpp observe/endAttempt/discardObsoleteEvents | Confirmed lifecycle gaps by code; the supplied logs do not identify the user's exact failing timeout Attempt. Reproduce these boundaries with production runtime/recorder/storage/receipt tests before attributing that incident. |
| Automatic return calls only Recorder.finish; manual onQuit skips endAttempt when autoExitRequested | PlayLayer.cpp and finishSurvivalReplayForReturn | Recorder finalization and official End ownership can diverge. One owned idempotent closing path is required. |
| Official cutoff can close at the last accepted progress; Recorder and Storage require full local terminal progress to equal it | Recorder receipt callback; Storage.reconcile; server locked cutoff | Do not raise official score or loosen equality. Preserve the complete immutable capture and expose only a provable complete Native prefix matching the official receipt, otherwise retain pending. |

Older beta.116/117 failures and beta.118 admission failure remain regression
context, not evidence that beta.119 has the same exact cause. The extended
23:16:41 log overlaps the shorter file and is not counted twice. Entropie live
RECAPTURE remains outside this replay/persistence task.

The existing v2/v3 boundaries, initial physics, Native checks, CBF hook lease,
no-save replay ownership and official receipt gates must remain. No production
mutations or device validation have been performed for beta.120 yet.

## Implemented boundary and recovery policy

`SurvivalReplayClock.hpp` records observed FRAME / DELTA / STEP / INPUT entry
and exit values. Source capture is read-only. Replay supplies those recorded
external input-clock assignments before the clock is consumed; dt, Native
steps, queue consumption, player physics, RNG and final evidence keep their
existing checks. An unchanged boundary rejects an unexpected write. Legacy
v2/v3 uses only its recorded initial / prior final / dispatch clock evidence;
missing essential state is rejected, and original files are never rewritten.
The exact proprietary instruction writing the host clock is still unverified.

The first physical failure now compares `physics` with `physics`; pose failures
name the actual failed predicate, excluding unrelated visibility/camera fields.
The failure log includes a bounded input-clock observation trace.

Official Start reserves an immutable End envelope. Round/session changes and
Scene returns close that exact owner; viewer / Free Play / replacement Scene
callbacks cannot close another Attempt. The End is journaled atomically and
retried using its saved match/round/generation/level and stable eventId. Receipt
reads determine whether it already ended. Closing a recovered orphan waits for
the disk recovery result and never creates a new official Attempt.

Native frame records are staged until the complete final pose and required
CBF/clock proofs exist. Scene retirement discards the incomplete in-flight
frame and seals only earlier complete frames as PARTIAL_RECOVERED. Epoch checks
retire callbacks already on the Native stack. Repeated terminal requests keep
the first reason and cannot append another terminal/footer.

If official timeout progress precedes the full local capture, Storage promotes
only a continuous complete Native prefix with exactly the same official
quantized score and a genuine native Clear when required. The full original
hash-chained capture remains immutable. Manifest `verifiedFrameCount` bounds
playback, and OFFICIAL_PREFIX is labeled PARTIAL REPLAY. No matching evidence
means pending is retained; no progress, pose, Clear or score is fabricated.
History and owner Receipt share the same tie-aware best-ended-Attempt selector.
This adds a read-only `bestAttemptId`; no DB migration, reward or scoring change.

## Validation limits

The supplied three frame-0 host timestamps are reproduced in the production
clock policy fixture. Production CBF controller, render scheduler, exact clock,
Storage/Recorder and lifecycle fixtures cover repeated sessions, 0.5/1/2/4x,
Pause/Restart/Seek, stale callbacks, timeout closure retries, immutable prefix,
partial frame retirement and previous best preservation. Their Native bodies
are explicit doubles, not proprietary Geometry Dash physics. Actual GD2.2081
on iOS with CBF1.5, sound, visual camera/visibility and device scene teardown
remain unverified in this environment. These checks cannot establish that
later real-device DESYNC has been eliminated.
