# Solo live spectator Stage 1 — beta.104 publication

Starting task main: `53a00958da3bb01d19fe235c61962770e4296d53` (beta.102).
Implemented source: `3164b1174215a2c9ca4346a0e6a5bad09cd4012f`;
Apple integer correction: `8016459f4778b251b40ebacad17ee6e826a93bb0`;
compiled-package contract: `30d9c4a7a905f499da220f0d2c09b8871c350ad6`.
[Implementation, exact native call order and evidence limits](solo-spectator-stage1-beta103.md).

## Why a fresh immutable version

All four Release builds, all four Debug builds and all four Debug OFF checks on
30d9c4a7 passed. Full native CI was 300/300, rules 288/288, server 630 passed /
1 optional skipped, PostgreSQL HTTP integration 52/52, with 1,050 simulated
matches and a ten-hour virtual soak. Typecheck/build and real package checks passed.

The earlier 8016459f Debug workflow published beta.103 even though its iOS Debug
OFF package check failed under the old verifier contract. Its native runtime is
identical to 30d9c4a7 (that next commit changes only tests/docs). The final Debug
publish therefore correctly failed closed because immutable bytes already existed.
Both public beta.103 channels were downloaded in full and verified; neither was
overwritten, removed, or activated as the server's required version.

To retain exact final-source publication provenance and a successful gated run,
this task continues as beta.104. Only synchronized version metadata/changelog and
the publication prerequisite change; spectator implementation is unchanged.
Debug publication now also requires ios-debug-off and additional-debug-off, so a
failed OFF platform cannot publish while Debug ON succeeds. A workflow contract
regression verifies the full dependency and fail-closed default Actions semantics.

The beta.104 source is frozen for one `[publish-r2] [no-deploy]` push. Both channel
builds/publication and Docker preparation run concurrently. Singapore activation
will follow only after both full public packages (hash/size/identity/channel,
native architecture/detours, fonts/assets) verify. Existing pinned-host, rollback,
DB and immutable-object protections remain. No production migration or data reset.

Physical Geometry Dash trigger execution and input/scene behavior remain untested
on devices. Full dual, arbitrary historical world catch-up, advanced camera /
platform parity and Survival opponent-selection are Stage 2 limits, as detailed
in the implementation report. Actual publication/activation evidence follows.
