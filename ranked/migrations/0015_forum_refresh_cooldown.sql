BEGIN;
-- Manual requests are limited per GD account, across sessions/workers. Keep
-- this separate from the provider cache and automatic CoJong refresh leases.
CREATE TABLE IF NOT EXISTS ranked_forum_refresh_cooldowns (
  gd_account_id BIGINT PRIMARY KEY CHECK (gd_account_id > 0),
  next_refresh_at TIMESTAMPTZ NOT NULL
);
COMMIT;
