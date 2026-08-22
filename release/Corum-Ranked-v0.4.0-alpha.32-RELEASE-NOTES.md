# Corum Ranked v0.4.0-alpha.32

- Fixed Config-driven Ranked BGM being silent after successful resource downloads.
- Ranked now fully removes the previous Geometry Dash music channel before starting a downloaded Ranked track.
- Main music pause/volume state is normalized before playback while preserving the user's Geometry Dash music volume.
- Added FMOD playback verification and automatic retry instead of marking a failed `playMusic()` call as successful forever.
- Configured song start offsets are applied only after the new channel is confirmed active.
- Private cached downloads are validated as real audio files so HTML/error responses cannot pass the resource gate.
- Geometry Dash menu music is cleanly recreated when leaving Ranked.
- Gameplay rules, Ranked scoring/MMR, server APIs, Apps Script config fields, and Bot logic are unchanged.
