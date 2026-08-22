# Corum Ranked v0.4.0-alpha.30

- Fixed Ranked lobby/resource BGM downloads failing before song metadata was loaded.
- Removed direct `MusicDownloadManager::downloadSong()` use from Ranked resource acquisition.
- Ranked now fetches Geometry Dash song metadata and downloads audio through Geode WebRequest into a private Ranked cache.
- Existing Geometry Dash-downloaded songs are reused without duplicate downloads.
- Existing RANKED RESOURCES download/retry/progress UI and all gameplay/MMR rules are unchanged.
