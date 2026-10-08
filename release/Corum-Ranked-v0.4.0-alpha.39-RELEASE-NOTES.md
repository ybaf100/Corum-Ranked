# Corum Ranked v0.4.0-alpha.39

- Fixed Attempt End HTTP 400 loops caused by raw progress values with more than three decimal places.
- Prevented one invalid Attempt event from blocking all later Starts, Ends, and Clears forever.
- Removed the fake first `Start -> End 0 -> Start` attempt created by Geometry Dash's delayed initialization reset, while preserving genuine early deaths/Clears.
- Fixed the last-ten-seconds HUD: it now displays `FINAL ATTEMPT 10.0` immediately at 3:00 and counts down visibly to `0.0`.
- Enforced the 3:10 new-attempt cutoff without interrupting an attempt that started in time.
- Fixed frozen Level Complete controls and automatic transition to the two-Clear spectator/result screen.
- Shows `CLEAR RECORDED / SYNCING RESULT` instead of `OPENING LEVEL / WAITING MAP` while a second Clear is awaiting acknowledgement.
- Preserved the real Geometry Dash LevelInfo Start-button path and music through the pre-game countdown.
- Added server compatibility normalization and tests for both 3:00/two-Clear rule intersections.
- Fixed the `Validate Corum Ranked` Actions mock-sheet fixture that falsely reported missing alternate map codes even when the live Spreadsheet was complete.
- Requires the matching `v0.4.0-alpha.39` server version gate. No database migration or Apps Script update is required.
