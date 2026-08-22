# Corum Ranked v0.4.0-alpha.33

- Fixed intermittent Round attempt/score loss caused by visual Start/End events waiting behind the HTTP FIFO when the server clock reached a result first.
- Added durable PostgreSQL Start Leases plus normal/Death Match ACK-loss idempotency; requires `ranked/migrations/0002_attempt_start_leases.sql` before the alpha.33 server is deployed.
- Fixed Death Match incorrectly consuming the third visual attempt before it was actually started. Players now receive exactly three visual attempts; attempt 4 remains server-blocked.
- Next Round / MATCH POINT / Death Match information remains visible for 5 seconds before the normal Geometry Dash level gate opens.
- Reworked Ranked BGM to a Ranked-owned FMOD stream, added iPadOS/iOS foreground recovery and playback watchdogs, and set Ranked BGM to 80% of the current Geometry Dash Music Volume.
- Scoring/MMR, map-pool, ban, two-Clear, FINAL/LAST ATTEMPT deadline rules, Debug Bot rating behavior, and Apps Script schema are unchanged.
