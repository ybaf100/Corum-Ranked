# beta.45 — menu readability, BGM scope and connection grace

Base: connected `ybaf100/Corum-Ranked` main
`390f77d0f96d66fab5c6327d06f955cabb31d7f3`.

- Ranked lobby, matching/preparation, results, history, achievements and menu
  dialogs use a separate larger typography policy. Gameplay HUD and native
  countdown font scales keep their existing policy. Long captions remain within
  their width; full map titles still wrap without truncating the original text.
- Player information and Settings shrink from 350×285 to 300×252. Information
  rows and values get larger authored sizes. Menu captions render above the
  surface's opaque fill and chrome, fixing the hidden information Close and
  Settings forum-refresh captions. Korean labels include their required glyphs.
- Personal volume keeps the saved `ranked-volume` value, but the master-output
  DSP and its process scheduler are removed. Only Corum-owned menu BGM channels
  join a dedicated FMOD group. Changing gain updates active and retiring BGM
  together without replaying/downloading/seeking. Gameplay, ordinary GD music,
  SFX and GD volume preferences do not join this group. Handoff/close/config
  teardown stops every owned channel before releasing the group.
- Entry countdown presentation latches non-increasing remaining time for its
  exact scene. HTTP clock corrections cannot redisplay 3 after 2 or restart a
  completed digit animation. The caption changes only when its digit changes,
  instead of rebuilding glyphs every heartbeat. A new round creates fresh
  presentation state. Gameplay GO continues to require the original unmodified
  server clock, both-entered certificate and native scene/attempt authority.
- Global session admission lease increases from 20 to 90 seconds across lobby,
  queue, gameplay and results. Heartbeat remains 5 seconds. Explicit Close
  releases immediately; crashed processes expire after the last successful
  heartbeat's 90-second lease. Expired/revoked tokens and stale replies cannot
  revive a connection or release a replacement device's session.
- Manifest, server exact version gate, workspace versions and lockfile all use
  `v0.5.0-beta.45`. External image-pack inputs are unchanged.

Validation: 122 source/native-boundary tests passed across the full run and
focused reruns after adapting the UI/audio boundary doubles; 114 rule tests and
219 server tests passed. Type-check, server/rules builds, five image-pack policy
tests and `git diff --check` passed. New FMOD group operations also compile
against the actual Geode 5.10.1 FMOD headers. Countdown tests reproduce a
backwards observation through the real ServerClock and check 60/120/144/240 Hz
presentation, next-round reset and unchanged native release gates. Audio tests
exercise both crossfade channels, zero/recovery, native music/SFX isolation,
unchanged play/seek counts and group teardown.

The user requested direct main push without waiting for workflow execution.
No complete platform build or device verification was performed for beta.45.
The layout reference uses an existing packaged font atlas, not a GD capture.
