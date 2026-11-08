-- Survival elimination effect cosmetics: only the equipped choice is stored.
-- Unlocks are derived from peak tiers and the claimed AP grade.
ALTER TABLE ranked_profiles
    ADD COLUMN IF NOT EXISTS elimination_effect TEXT NOT NULL DEFAULT 'DEFAULT';
DO $$ BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint WHERE conname = 'ranked_profiles_elimination_effect_check') THEN
        ALTER TABLE ranked_profiles ADD CONSTRAINT ranked_profiles_elimination_effect_check
            CHECK (elimination_effect IN ('DEFAULT', 'BOLT', 'SLICE', 'SHATTER', 'VORTEX'));
    END IF;
END $$;
