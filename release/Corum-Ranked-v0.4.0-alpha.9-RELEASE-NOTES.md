# Corum Ranked v0.4.0-alpha.9

## **Expand Assets below to download the mod. Download the file ending in `.geode`.**

# v0.4.0-alpha.9
- Fixed first Ranked session creation failing with a generic Internal Server Error when Google Apps Script CSMP requests cold-started past the old timeout.
- Added an independent 30-second CSMP source timeout and one automatic retry.
- CSMP timeout now returns HTTP 503 with `CSMP_SOURCE_TIMEOUT` instead of HTTP 500.
- Shared CSMP tier definitions are cached server-side; per-player records remain fresh for the one-time initial seed.
- Increased only the initial Geode session request timeout so the server-side retry can finish.
- Corum Integration is unchanged.
