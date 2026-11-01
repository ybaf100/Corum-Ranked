-- Survival promotion series (beta.130). NULL = no active series. Additive; no
-- existing LP, tier, ledger or history row is changed.
ALTER TABLE sur_profiles ADD COLUMN IF NOT EXISTS promotion JSONB NULL;
