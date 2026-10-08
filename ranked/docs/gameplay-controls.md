# Match gameplay controls (beta32)

These controls apply to normal Ranked/Debug Bot matches, not Placement or ordinary
non-Ranked gameplay. Existing scoring, rating calculations, round deadlines and
timer-zero final Attempt permissions remain in force.

## Surrender

The native pause menu adds **SURRENDER**. Choose **ROUND** or **MATCH**, then
confirm; CANCEL leaves gameplay unchanged. ROUND concedes only the current round.
MATCH ends the match as a defeat. In Death Match either scope concedes the decisive
match. Already committed scores/Clears remain; open runs close as invalid
SURRENDER attempts, never synthetic deaths/Clears/awarded scores.

`POST /api/ranked/matches/:id/surrender` requires a session and match token:

```json
{"scope":"ROUND","clientEventId":"unique-event","roundNumber":2,"deathmatchSequence":0}
```

The match lock, persisted per-player event IDs and exact round/DM context make
retries idempotent. An old request cannot concede the next round. A committed
concession increments the existing state version; older `/state` replies cannot
revive the abandoned attempt. The client lets already completed local results
receive their normal End ACK before accepting a surrender action. It does not
turn ordinary timer expiry into a concession or terminate the final Attempt.

## Pause budget

Each player has **30,000ms for the entire match**, cumulative across all rounds
and Death Match. This is a per-player budget, not 30 seconds per round or a shared
budget. Pause shows the remaining time; beta.46 forfeits the entire match at zero.
Wall-clock round deadlines continue; pause does not extend Ranked time windows.
Placement remains unchanged.

`POST /api/ranked/matches/:id/pause` uses the same session/match authorization:

```json
{"leaseId":"unique-pause","paused":true}
```

Send `paused:false` with that lease to resume. Serialized requests and retired
lease IDs prevent delayed Start/End ACKs from reopening a closed pause or closing
a newer one. Server state persists the budget on the match; changing rounds,
restarting the service, or returning from a scene does not refill it. The client
monotonic timer starts immediately and accepts server observations only when they
reduce the remaining budget. A paused publisher keeps its same spectator
Attempt/generation with a frozen gameplay timeline, rather than a 30-second gap.

From beta.46, an open Pause with 0 seconds left forfeits the **entire match**.
The server checks the active lease before processing Resume or normal match
advancement, so a late End cannot erase an exhausted Pause. Timely Resume closes
the lease and prevents expiry while the player is playing. Expiry is persisted
as `surrender.scope=MATCH` with `reason=PAUSE_BUDGET_EXHAUSTED` and uses the same
score/rating settlement and authenticated scene handoff as manual surrender.
Death Match uses the same match-wide budget. The Pause timer turns red at 5
seconds or less; its warning is a child of that exact native Pause, removed on
Resume and recreated on reopening. No additional native detour is installed.

## Server-global live spectator setting

The Notion [UI/UX document](https://app.notion.com/p/3ce44344d8ba81c18b4ef00ad1f2b94e)
defines Finished as no legal future Attempt Start. Its existing wait UI shows the
opponent's name, live percentage and score comparison. With live spectator OFF,
Finished returns to this Ranked wait screen rather than entering the native
spectator scene. OFF does not restore dummy gameplay/result authority.

Debug Spectate adds **SERVER LIVE SPECTATOR: ON/OFF**. The label reflects the
server ACK; an unknown setting refreshes rather than assuming a local value.
There is no personal preference. The setting persists in PostgreSQL and applies
to all production publisher/viewer routes and decorated match state, including
other server instances on their next read. Debug RECORD/TEST remain independently
available for diagnosis while production live is OFF.

`GET /api/ranked/debug/spectate/live-setting` requires a session plus the existing
Debug build header/server opt-in. POST additionally uses the same constant-time
Debug server password check as RECORD/TEST:

```json
{"password":"server-debug-password","levelId":88350599,"formatVersion":2,"liveEnabled":false}
```

The persisted revision prevents older match responses from reversing a newer
setting. Switching OFF retires production relay streams and presentation while
keeping the finished player's safety firewall and percentage-only wait UI.

## Deployment

Migration `0012_gameplay_controls.sql` adds match pause/surrender state and the
`ranked_runtime_features` table (default live ON). It is in the startup-required
migration ledger and is applied under the existing PostgreSQL advisory lock.
Deploy server and beta32 client together: the exact server version gate is
v0.5.0-beta.32. Packet protocol v2 and Debug recording format v2 remain unchanged.
