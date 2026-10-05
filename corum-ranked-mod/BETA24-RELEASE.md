# v0.5.0-beta.24 — Debug Spectate verified native launch

The supplied iPadOS beta23 log shows an accepted online VOICE lookup returning a geometry-empty SearchResult even though subsequent attempts already have a Saved VOICE object with 415,604 bytes of geometry. Debug Spectate retained the online pointer for LevelInfo instead of reusing the downloaded native object.

- Keep the fresh online metadata lookup as identity authority. After attestation, re-query the native Saved cache and adopt its pointer only when every PreparedLevelProof identity field matches and downloaded geometry is present. Mismatch/missing geometry retains the verified online/native download path. The primitive is called only by Debug Spectate; regular Ranked entry is unchanged.
- Preserve native LevelInfo replacement/type after init/download. Verified existing geometry does not trigger a redundant Debug geometry request.
- Require the effective MusicDownloadManager/Jukebox song file (or a genuine native resource). A stale downloaded flag is insufficient. Existing files skip acquisition; absent files use the native song widget, then automatically invoke vanilla Play once file readiness and native busy/modal guards permit it.
- Transfer preparation ownership before native LevelInfo construction. Popup close after transfer cannot cancel the LevelInfo owner; explicit popup Cancel before transfer and LevelInfo Back still finalize incomplete recordings. Add cancellation reasons and bounded preparation/PlayLayer/recording logs.
- Compile production proof/adoption, LevelInfo gate and Debug runtime methods against native doubles covering identity mismatch, native pointer replacement, iOS/Jukebox effective path, waiting-to-ready, busy/modal, handoff and explicit cancellation. Keep server-backed HTTP fixtures, official Debug OFF and Phase 1–3 regression suites.
- Synchronize mod/server exact client gate to beta24. No server recording/protocol/rating changes, manual Release or workflow rerun.

## Device verification still required

The log proves the pointer mismatch; its `Jukebox download=true` lines alone do not establish actual file readiness, and the old log does not identify the cancellation caller. No real iPadOS run was performed in this environment.

Retest RECORD twice on VOICE. With matching downloaded Saved geometry expect: `preparation metadata verified` → `using downloaded saved VOICE geometry` → `map ready` → `song ready path=...` → `invoking vanilla Play` → `PlayLayer entered` → `RECORDING active`. If the effective song file is absent, expect one `song acquisition requested song=1184472` before readiness. A missing/mismatched Saved object should instead use `native geometry acquisition requested`. Normal scene handoff must not log `preparation cancelled`; explicit Back/Cancel must log its reason and finalize incomplete. Confirm the RECORD HUD, 50 ms server pose upload and terminal persistence.
