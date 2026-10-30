# beta.121: unsealed timeout capture recovery

Base main: 1a88c3956cac4780c0b6773a904662e288636554 (beta.120).

A new production Storage regression failed before this change: a killed process
left durable Native frames at 10/20/30 with no footer, but the official closed
receipt ended at 20. The prior partial-file path checked the last pose (30)
and retained pending before considering the evidenced prefix. It preserved the
file and existing best but could not make that valid timeout replay available.

Recovery now validates the same complete owner/map/initial/CBF/clock evidence
as sealed captures before any file mutation. It appends a PARTIAL_RECOVERED
seal using real captured progress, leaves every valid existing byte intact,
and exposes only the exact official prefix. A mismatched map leaves even the
torn suffix untouched; absent proof remains pending. No server score, reward,
Native pose, input or original record is fabricated or overwritten.

The failing regression passes after this change, together with production
Recorder, lifecycle and clock tests. The full release CI rechecks all Native,
Rules, server, PostgreSQL and platform packages. Real GD/device execution is
unavailable and remains unverified.

During the beta.120 release, GitHub workflow cancellation returned HTTP403
with the available Actions integration. Neither channel was public at that
check. The immutable beta.120 release is therefore preserved and this fix uses
a distinct beta.121 release and the existing gated Singapore activation path.
