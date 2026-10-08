BEGIN;
-- Public external statistics have no rating authority. CoJong is cached even
-- when that GD account has never created a Ranked profile.
CREATE TABLE IF NOT EXISTS ranked_forum_records (
  gd_account_id BIGINT NOT NULL CHECK (gd_account_id > 0),
  provider TEXT NOT NULL CHECK (provider IN ('GDKF', 'POINTERCRATE')),
  gd_username TEXT NOT NULL,
  external_id BIGINT,
  external_name TEXT,
  rank INTEGER CHECK (rank IS NULL OR rank > 0),
  status TEXT NOT NULL DEFAULT 'PENDING'
    CHECK (status IN ('PENDING', 'RANKED', 'UNRANKED', 'UNLINKED', 'ERROR')),
  fetched_at TIMESTAMPTZ,
  last_success_at TIMESTAMPTZ,
  refresh_started_at TIMESTAMPTZ,
  PRIMARY KEY (gd_account_id, provider)
);
COMMIT;
