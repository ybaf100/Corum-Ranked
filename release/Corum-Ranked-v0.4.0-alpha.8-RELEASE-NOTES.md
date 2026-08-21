# Corum Ranked v0.4.0-alpha.8

## **Expand Assets below to download the mod. Download the file ending in `.geode`.**

# v0.4.0-alpha.8
- Fixed a CI-only PGlite test isolation bug that could reuse the deterministic player UUID `00000000-0000-4000-8000-000000000011` across session tests.
- Session/profile creation tests now clear their database rows before each test case.
- The active-only mod allowlist behavior from alpha.7 is unchanged.
- Production player/session IDs still use cryptographically random UUIDs; no production database behavior was changed.
- Corum Integration is unchanged.
