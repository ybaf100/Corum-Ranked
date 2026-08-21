# Corum Ranked v0.4.0-alpha.16

- Fixed `START WITHOUT SONG` errors by using Geometry Dash's normal `LevelInfoLayer::onPlay` transition and bypassing only the vanilla no-song warning.
- Fixed custom-song downloads by requesting song info with download intent and using GD's custom-song download pipeline as the direct fallback; song status now reflects GD's actual active download state.
- Added Corum difficulty/rating colors to revealed maps and Ban cards.
- Fixed false local Ban confirmation: the UI now shows `BANNING...` until the server confirms the player's private ban, preventing rejected late bans from appearing confirmed.
- Added server integration coverage for private Ban acknowledgement and for excluding confirmed banned maps from Round selection.
- No Apps Script or database migration changes. Corum Integration is unchanged.
