# Solo Ranked native entry heartbeat (v0.5.0-beta.83)

## Device evidence
From the attached iPadOS Geode 5.11.2 / GD 2.2081 log (2026-10-18 at 20:13 KST), exact match `2e499675-a265-406c-8330-2aeda40fb55b` fetched level 89425733 (297,732 geometry bytes), entered `ROUND_ENTERING`, and created its native PlayLayer at 20:13:26. `[RankedEntry] native hold` was logged, but no `native scene entered`, `gameplay released`, or GO appeared before Pause/exit at 20:13:35.

Railway proxy records at 11:13 UTC show both clients repeatedly fetching `/state` until the native launch, but **neither posted `/entered`** during the stalled interval. A later `/state` appears after the user exits. These observations establish an entry heartbeat gap, not a map/download failure; they do not establish the native iOS scheduler implementation responsible.

## Change
`RankedRoundPreparation` already owns a process-lifetime main-thread CCNode update. Its update now pumps the existing `PlayLayer::blockEntryGameplayUpdate` gate on the *exact* retained native scene, at most once per 100 ms, only while the official Solo Ranked entry is held. The gate alone still sends the authoritative `/entered` acknowledgement, waits for both players and server `startsAt`, checks live scene and entry context, and restores input/music at GO. No unconditional unpause, synthetic attempt, direct server mutation, or native `onEnter` replay is introduced. Ordinary levels, Placement, Survival, spectator, exited/retiring scenes and other owners are excluded.

## Verification
Source regression asserts the independent scheduled path and exact-owner constraints; existing native entry and session tests continue to cover server start certification. GitHub CI validates compiler/tests. Real iPadOS gameplay and a return-to-lobby/re-entry sequence still require hands-on acceptance. If GO still fails, capture full Geode logs and inspect independent heartbeat, `/entered` proxy status and the server `startsAt` certificate.
