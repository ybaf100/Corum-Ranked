# v0.5.0-beta.26 — Debug Spectate native Play activation

Baseline: authoritative remote main `1ea186c420368f6e9d98a1426e07b255ff92e5fe`, beta25.
Mod manifest and server exact client gate advance together to beta26.

The iPadOS beta25 VOICE log reaches map ready, song ready and invoking vanilla
Play, then stays in LevelInfo until explicit Back finalizes an incomplete RECORD.
The launch directly called `LevelInfoLayer::onPlay(nullptr)` while the Debug-owned
onPlay hook blocked every callback. Readiness was already latched and polling
stopped, so there was no retry or acknowledgement of native scene entry.

RECORD and TEST now share the verified native Play item identity lookup, require
its LevelInfo listener/selector and a non-busy page, enable it, and call activate.
The native listener/selector are retained unchanged. A separate one-shot Debug
latch is consumed before vanilla onPlay forwarding; manual or duplicate dispatch
cannot create another PlayLayer. Layer and item remain retained during dispatch.

A four-second watchdog retries an unaccepted activation, keeping the server scope
alive. LevelInfo onExit or Debug runtime scene entry stops the watchdog. Explicit
Back clears pending UI callbacks and preserves existing incomplete finalization.
Existing online metadata attestation, matching Saved geometry adoption, effective
song/Jukebox file checks, server-backed RECORD/TEST, protocol/format v2 and Phase
1–4 safety/rendering/camera/state/isolation remain in place.

Validation: 59 native/source tests, six strict C++ domain executables, 114 rules
and 157 server tests, TypeScript typecheck/build and both Apps Script checks pass.
The new native double executes production Play item identity lookup, member
selector dispatch, Debug onPlay forwarding, PlayLayer Debug entry and real runtime
mode transitions. Missing/wrong/busy callbacks, duplicate/dropped dispatch,
RECORD/TEST, watchdog success/failure and Back are covered. Direct-call and open
latch mutants compile and are rejected by the executable regression.

Expected RECORD markers after map/song readiness:

```text
[DebugSpectate] activating verified native Play button
[DebugSpectate] native Play callback forwarded
[DebugSpectate] LevelInfo exit accepted
[DebugSpectate] PlayLayer entered level=88350599
[DebugSpectate] RECORDING active
```

PlayLayer init may acknowledge entry before the outgoing LevelInfo receives
onExit; both lifecycle orders are covered. TEST uses the same activation path and
enters the existing server viewer/renderer instead of emitting RECORDING active.
Real iPadOS confirmation remains necessary. No manual Release workflow is run;
automatic Debug/Validate CI results belong to the final report.
