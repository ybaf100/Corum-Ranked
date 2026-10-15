# Common Admin database reset — beta.71

The user explicitly clarified that Common → RESET_DB deletes all Ranked data,
including accounts and user records. It no longer means just resetting ratings
while retaining accounts and sessions.

Solo and Survival RESET_DB retain their existing independent scopes. Common
RESET_HISTORY remains the shared achievements/AP/title reset for the selected
player. Only Common RESET_DB performs the full deletion.

The existing Admin identity/password/RESET confirmation and live-session
transaction checks still apply. Survival admission is fenced and the existing
active-game/unsettled-payment protection is retained. All application tables in
the current schema (`ranked_*`, `sur_*`, `debug_spectate_*`) are truncated in one
transaction, with identity sequences reset. This includes accounts, sessions,
profiles, both modes' matches/attempts/results/LP ledgers, Placement, achievements,
AP/titles, forum caches, map history/cache, queue/request data, debug recordings,
configuration snapshots, feature rows and per-song volume overrides.

Tables, constraints and `ranked_schema_migrations` remain installed. Other
schemas are untouched. Table names come from PostgreSQL's catalog and are quoted;
no request field can become a SQL identifier. There is no DROP DATABASE/SCHEMA or
migration replay. Normal configuration refresh can repopulate configuration
state, and a new login creates new default user profiles.

The response adds `accountsDeleted: true`. The client closes its deleted session
and stops using old profile/auth projections instead of refreshing them with a
token that no longer exists. The confirmation explicitly states that all account
and session data will be deleted and login is required again. Existing packaged
Korean glyphs are reused; font assets/charsets and their package budget are unchanged.

Regression coverage checks every application table is empty, schema history
survives, the old token returns 401, and a fresh login after a completed real
PostgreSQL-backed online match succeeds with a new account identity. Scope
isolation tests for Solo/Survival and authentication tests remain in the full suite.

This document describes the Admin feature. The production database was not reset
as part of implementing or testing it.
