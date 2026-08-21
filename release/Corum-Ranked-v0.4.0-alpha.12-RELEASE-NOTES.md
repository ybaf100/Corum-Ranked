# v0.4.0-alpha.12

- Added a dedicated Geometry Dash-style Ranked menu scene opened from a new crossed-swords button on the title screen.
- Fixed Ranked map `Load Failed` loops by requiring complete downloaded level data before launch and entering through Geometry Dash's normal LevelInfoLayer play path.
- Reworked song downloading to fetch SongInfo first, support all `m_songIDs`, retry stalled requests, and keep background downloads alive after the 20-second audio wait limit.
- No Apps Script, Neon migration, Ranked rule, or Corum Integration changes are required for this release.
