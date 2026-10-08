# Corum Ranked v0.5.0-beta.1

This beta introduces visible LP backed by hidden MMR, placement outcome tracks and cutoff-safe placement, promotion series, tier-floor/demotion protection, streak and performance weighting, new LP/result presentation, immediate Ranked music transitions, and the revised 50%/75%/Clear scoring values.

It also fixes the reported LevelInfo `STARTING -> FAILED` loop by removing the unstable first-menu-child assumption and retaining the vanilla Play callback path.

Deployment requires the matching Apps Script configuration, PostgreSQL migration `0005_v050_rating_system.sql`, server, and exact `v0.5.0-beta.1` client gate. Official builds leave Debug Bot Match disabled and do not link the Debug Bot/Force Win/Lose implementation files.
