# Corum Ranked v0.4.0-alpha.7

## **Expand Assets below to download the mod. Download the file ending in `.geode`.**

# v0.4.0-alpha.7
- Ranked now checks only currently active Geode mods (enabled + loaded) against the server allowlist.
- Installed but disabled mods no longer block Ranked.
- Inactive user mods are omitted from the client environment payload; CBF is retained only when needed to report that the mandatory dependency is inactive.
- The server applies the same active-only filter independently for compatibility with older clients.
- CBF remains required and must be active with the configured Ranked-safe settings.
- Corum Integration is unchanged.
