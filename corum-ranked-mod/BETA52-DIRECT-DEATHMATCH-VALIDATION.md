# beta52 direct Force Death Match

Base: authoritative `ybaf100/Corum-Ranked` main
`9d2ff9ceef126338b3ee619d46a6cd8f6a6d9194` (beta51).

- The existing `TRIGGER_DEATHMATCH` debug scenario immediately enters
  `DEATHMATCH_PREPARE`. It creates no Round 1-3 results, Attempts or bans.
  The series starts at `DEATHMATCH_REQUIRED` with zero round wins.
- The Debug Bot supplies only its own resource READY. The real player's
  existing preparation screen counts ten seconds and sends READY automatically
  when resources are ready. Resource downloads retain their safety budget.
- Keep the existing native scene-entry certificate, in-game GO countdown and
  three-Attempt authority. The server never impersonates the player's native
  entry or starts their gameplay while the preparation screen is open.
- The locked transition is idempotent. A recovered driver can finish creation
  after interruption without producing a second Death Match or entering bans.
  Real PvP and other debug scenarios cannot use the direct transition.
- The Ranked lobby heading is `Ranked`. Client manifest, npm package versions
  and the server's exact client gate advance together to `v0.5.0-beta.52`.

Local validation:

- Rules build and server source/test typecheck passed.
- Focused server suites: 49 tests passed across Debug Bot integration,
  durable scenario checkpoints, Death Match flow and client version gating.
- Four source/native tests passed, including actual preparation automation at
  9.9/10 seconds and Death Match Attempt/entry boundaries. Native tests use
  SDK/transport boundary doubles; they do not establish device acceptance.
- Server TypeScript build and final manifest/package consistency checks passed.

External image pack inputs, scoring/MMR policy and native hooks are unchanged.
No GitHub workflow result inspection or manual Release execution is required
for this push.
