# beta.47 — rematching after both players disconnect

Base: connected `ybaf100/Corum-Ranked` main
`fd25ac74b546528a54925532b85c66c34ebebe31` (beta.46).

The session lease change did not fix abandoned-match admission. Session expiry
or explicit Close releases the account connection and its queue row; it does
not settle `ranked_matches`. Existing reconnect handling runs during match API
requests. If both clients leave, neither advances that old match. A new session
can be admitted while Queue's active-match guard still rejects the stored match.

Public Queue join and Debug Bot entry now settle the authenticated player's
existing active matches in a separate transaction before taking queue/profile
locks. This reuses MatchService's existing authoritative advancement, including
the match's stored reconnect grace/failure policy, Pause expiry, result/rating
idempotence and normal cancellation. It never renews either match heartbeat.
Both expired participants cancel under the existing reconnect policy; a single
disconnected participant keeps that policy's normal loss/winner settlement.
A still-live match continues to block duplicate admission. There is no new
poller, timer, session timeout or native client hook.

The active-match admission check runs after acquiring the player's queue row,
so a concurrent matchmaker's committed match cannot be overwritten by a fresh
join. The check does not take match locks after profile locks; settlement keeps
the existing match-before-profile order. A rejected join rolls back only its
queue edits, retaining the prior match and any separately committed settlement.
QueueModule imports MatchModule and requires its canonical MatchService.

The database regression disables the new settlement call to reproduce both
players' active-match errors even after fresh sessions, then exercises the
real path. It covers process crashes after 91/3601 seconds, explicit Close,
exact reconnect-grace boundaries, unchanged match heartbeats, simultaneous
joins, one replacement match, no rating changes on cancellation, one-sided
rating idempotence, preserved full-match Pause expiry and Debug Bot admission.
PGlite serializes database transactions; its simultaneous-request case checks
service interleaving/idempotence, not real multi-connection PostgreSQL deadlocks.

Mod, exact server gate, workspace versions and lockfile use beta.47. No schema
migration or image-pack input changes are needed. Native Pause/UI/audio source
is unchanged. Validation: 234 server tests and 114 rule tests passed, including
34 focused gameplay/queue/Debug Bot cases; server type-check and build passed.
The PGlite fixture now uses its real transaction callback API instead of
interleavable manual BEGIN/COMMIT calls, so concurrent service requests do not
accidentally share a single transaction. Manifest/gate/workspace/lockfile checks
and diff whitespace checks passed. Platform build/device verification and
workflow-result polling are not performed; no manual Release is invoked.
