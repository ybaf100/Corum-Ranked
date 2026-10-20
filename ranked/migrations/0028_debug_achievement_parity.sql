BEGIN;
-- Enable Debug achievement parity from this release without resetting v2 data
-- or retroactively replaying the former Debug-excluded policy.
ALTER TABLE ranked_achievement_versions ADD COLUMN IF NOT EXISTS debug_enabled_at TIMESTAMPTZ;
UPDATE ranked_achievement_versions SET debug_enabled_at=CURRENT_TIMESTAMP
 WHERE version=2 AND debug_enabled_at IS NULL;
COMMIT;
