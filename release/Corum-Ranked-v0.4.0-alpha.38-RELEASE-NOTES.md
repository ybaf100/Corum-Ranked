# Corum Ranked v0.4.0-alpha.38

- Fixed frequent iOS crashes when releasing a UI button, including **QUEUE AGAIN**.
- Ranked buttons are no longer deleted and recreated every 0.2 seconds; live countdown and status text now update without replacing the active menu.
- Old queue/match polling responses are cancelled and ignored after a match is closed or re-queued.
- Fixed the round-start deadlock after `SENDING READY...` by launching the canonical downloaded level through a single verified `PlayLayer` initialization instead of repeatedly calling `LevelInfoLayer::onPlay()`.
- Added explicit keypad and touch-dispatch cleanup when leaving the Ranked scene.
- Updated the required client version to `v0.4.0-alpha.38`. Deploy the matching server version gate before testing this client.
- No database migration is required. Scoring, MMR/RP, map pool, ban rules, and attempt limits are unchanged.
