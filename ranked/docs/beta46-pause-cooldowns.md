# beta.46 — Pause expiry, forum cooldown and locked title buttons

Base: connected `ybaf100/Corum-Ranked` main
`86736cb25160d67570111871aaec4deddd6c508d`.

- The match-wide 30-second native Pause timer is red at 5 seconds or less.
  A Korean/English warning below it explains that 0 seconds forfeits the entire
  match. Both labels belong to the exact native Pause; Resume removes them and
  reopening creates them again. Placement and ordinary GD Pause are unchanged.
- At zero, the client submits the existing authenticated MATCH surrender once
  and keeps the native Pause attached for the authoritative concession handoff.
  The server also expires open leases before Resume/normal state advancement,
  using the existing surrender scoring/rating transaction. Exact-deadline
  Resume cannot evade the forfeit. Timely Resume closes the lease; playing with
  a closed lease never expires it. Death Match uses the same match-wide budget.
  Retries, polls and a late automatic surrender ACK cannot apply ratings twice
  or revive late gameplay/Clear evidence. The solved native Pause quit lifecycle
  is preserved; no global flags, native hooks or transport are added.
- Manual forum refresh is limited to one request per GD account per hour.
  `GET /api/ranked/session/forum-records` and authenticated POST `/refresh`
  expose `refreshAvailableAt` and `serverNow`. An atomic PostgreSQL claim
  enforces the limit across workers/sessions/restarts. Refused refreshes return
  the current cache with `refreshAccepted=false` without calling the providers.
  Settings shows a server-clock countdown and grays/disables the button during
  loading or cooldown; it restores the normal face when the deadline passes.
  An attempted refresh consumes the hour even on provider failure, while last
  good ranks remain cached. First-profile initialization and automatic CoJong
  initialization keep their independent source leases.
- Unacquired Achievement TITLE buttons and unearned progressive title levels
  gray their complete custom surface, including border, fill and readable
  caption. Existing title eligibility and red claimable-reward buttons remain.
- Startup migration `0015_forum_refresh_cooldown.sql` is idempotent and included
  in readiness checks. No Spreadsheet/API-key configuration is needed.
  Mod, exact server gate, workspace versions and lockfile use beta.46.
  Image-pack inputs are unchanged, so the existing workflow skips packaging it.

Validation: 114 rule tests and 225 server tests passed, including exact-deadline
Resume, timely Resume, match-wide accounting across rounds/server restart,
Death Match expiry, late ACK/Clear, unchanged ratings and concurrent/manual
forum requests across workers. The 124 source/native-boundary cases passed in
the full run plus a focused rerun after correcting a whitespace-only source
assertion; nine strict C++ suites also passed. The new Settings test executes
the real ServerClock with a one-day client clock offset, reopening, the exact
one-hour boundary and complete button desaturation. Type-check, rules/server
builds, manifest/gate synchronization and diff whitespace checks passed.

Full platform compilation and device verification are not performed locally.
The user requested direct main push without checking/waiting for workflow
results; no manual Release is invoked.
