# Corum Ranked v0.5.0-beta.19

Spectator Phase 2 from beta18 is preserved. Native CI exposed GD's ambiguous CCPoint/CCSize assignment overload in the two remote position-cache writes. Both now use explicit CCPoint values. The complete production-renderer lifecycle double also models the CCSize assignment overload so this compile regression is detected locally.

Client manifest/server exact gate advance to beta19, including beta18 rejection. No protocol, DB, gameplay authority, camera, Practice or Phase 1 safety changes. See BETA18-RELEASE.md for the architecture, MIT attribution and deferred phases. Native device visual/animation/entry QA remains separate from compile/tests.

The first beta18 push passed Validate run 37095840563 but failed native Debug run 37095840597 due to this code compile error. The correction is a separate non-force commit/push under the every-patch beta policy; no run is rerun or manually dispatched, and no Release is created/executed.

Corrective pre-push checks: 114 rules and 128 server tests, workspace typecheck/build, all 45 Node regressions and 15 strict remote policy scenarios passed. The renderer harness verifies that untyped brace assignment fails against the native-style overload before successfully compiling/executing the corrected production source. Preserved Phase 1/domain/Level Preparation/Apps Script checks also passed in the initial Phase 2 verification.
