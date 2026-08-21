# Corum Ranked v0.4.0-alpha.15

- Rebuilt client attempt transport as a FIFO start/end backlog so fast deaths, clears, resets, or restarts cannot overwrite an unacknowledged previous attempt.
- Fixed live score updates after Qualifying and preserved decimal Clear scores such as `120.1` for a `20.1%` Qualifying map.
- Fixed authoritative Clear-count synchronization and added immediate optimistic Clear/score feedback until the server acknowledgement arrives.
- Fixed manual restart/quit accounting so every visual attempt receives exactly one end event.
- Fixed the two-Clear acknowledgement race so a pending second Clear cannot auto-enter an illegal extra attempt before LAST ATTEMPT state arrives.
- Reworked Death Match around an exact local + server three-attempt budget, live attempt counters, and an authoritative fourth-attempt rejection.
- Centered and enlarged countdown/status text in gameplay and round preparation.
- No Apps Script or database migration changes. Corum Integration is unchanged.
