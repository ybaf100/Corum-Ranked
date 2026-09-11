# Corum Ranked v0.5.0-beta.14

Release candidate verification scope:

- Death Match uses exactly three authoritative Attempts and compares best raw progress percentage; Qualifying % and the normal attempt score curve do not participate.
- Death Match remaining-attempt HUD decrements only after authoritative Attempt End acceptance.
- Opponent progress exposes viewer-relative `Finished` only when no legal Attempt remains, including pending Start-lease protection and three-Attempt Death Match exhaustion; opponent-only Practice presence remains non-persistent and non-scoring.
- Placement renders stable Top 2 evidence rows while persisted Map Grade remains authoritative-Attempt-End only.
- Optional Jukebox is allowlisted without becoming required; Level Prepare continues to validate the effective Geometry Dash song path.
- Ranked FMOD streams are prewarmed outside competitive transition callbacks where possible.
- Rank visuals resolve verified external-cache assets before bundled fallback. Production Google Drive download remains disabled until real asset-pack metadata is supplied.
- Transition tracing records state receipt, audio handoff, visual decode/build, and first gameplay frame timing for actual-device hitch diagnosis.

The existing PvP timers, Bo3, Clear rules, LP/MMR formulas, Placement timers/Top-2 scoring, and beta13 Practice scoring isolation remain unchanged except for the explicitly documented Death Match best-percentage rule.
