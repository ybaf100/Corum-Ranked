# v0.5.0-beta.23 — Fresh Debug Spectate scope lifecycle

Corrective patch on beta22 main `cad744fc9b939506ad37fa79a870caa2fb183bfd`. The compiled production runtime test reproduced a previous expired deadline cancelling a fresh admission before its HTTP ACK. Fresh RECORD/TEST now clears the previous deadline, upload backoff and recording clock before issuing the request.

The regression injects an expired prior scope, starts a new real HTTP request through the client transport double, verifies preparation remains active, and drains an incomplete recording normally. Server persistence/replay clock, production viewer/interpolation/Remote Player/camera, Official-OFF isolation and Phase 1 safety are unchanged.

The exact manifest/server gate advances to beta23 and rejects beta22. Workspace package versions remain independent. See [harness contract and device checklist](../ranked/docs/debug-spectate.md). SDK compile/automated tests are not real-device RECORD/TEST or two-client E2E certification. No manual Release or workflow rerun.
