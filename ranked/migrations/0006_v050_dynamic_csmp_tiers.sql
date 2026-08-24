BEGIN;

-- CSMP progression names are configured by the operator and are independent
-- from the fixed Ranked display tiers. Preserve a normalized, bounded snapshot
-- of the one-time seed source without imposing an enum-style whitelist.
ALTER TABLE ranked_profiles
    DROP CONSTRAINT IF EXISTS ranked_profiles_initial_csmp_tier_check;

ALTER TABLE ranked_profiles
    DROP CONSTRAINT IF EXISTS ranked_profiles_initial_csmp_tier_format_check;

ALTER TABLE ranked_profiles
    ADD CONSTRAINT ranked_profiles_initial_csmp_tier_format_check CHECK (
        initial_csmp_tier IS NULL OR (
            initial_csmp_tier = UPPER(BTRIM(initial_csmp_tier))
            AND CHAR_LENGTH(initial_csmp_tier) BETWEEN 1 AND 80
        )
    );

COMMIT;
