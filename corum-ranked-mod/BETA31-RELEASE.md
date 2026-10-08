# v0.5.0-beta.31 — iOS ordinary Practice visibility-hook fix

## Device evidence

The user isolated the beta30 ordinary Practice crash to the global
`CorumRankedSpectatorDummyPlayer::setVisible(bool)` detour. With native probes
excluded, `visible-only` crashed and `special-only` did not. Removing only the
visibility detour, retaining `updateSpecial` and the other five PlayerObject
hooks, produced a fix candidate that the user subsequently reported as working.

The earlier crash signature was SIGABRT through Tulip 3.1.14 / Geode 5.10.1's
`Pool::getCommonHandler`, vector growth and allocation, with an animation
callback below it. This isolates a triggering Corum hook, but does not prove
OOM, a corrupted uniqueIndex, heap corruption, or the exact Tulip internal
failure. The separately reported Clear-transition crash has not been shown to
have the same signature. Build configuration, test duration and full feature
coverage of the latest successful device report were not specified.

The visibility-only candidate was built from diagnostic commit
`30a560baa2124e7b8242316422389265b51de4a0` in
[Actions run 37481669894](https://github.com/ybaf100/Corum-Ranked/actions/runs/37481669894).
The production patch starts from main `58d643d` and does not merge diagnostic
switches, startup probes or experimental Follow-Y replacements.

## Implementation

- Remove the global `PlayerObject::setVisible` override and its hook priority.
  Ordinary gameplay visibility calls no longer enter this Corum detour.
- Hide the exact authorized spectator dummy at the existing world/camera
  reconciliation boundary and after native PlayLayer post-update. This includes
  waiting for the first remote pose. Practice/test/retired scenes are excluded.
- Preserve native `updateSpecial` sampling at publisher-time 10 ms steps and
  the existing Windows inline compatibility path. No full player physics or
  replacement Follow-Y history is introduced.
- Preserve beta30's persistence firewall, final-attempt lifecycle, stale
  Attempt protection, Spider timeline boundaries, World Driver crossing
  history, camera ownership and adaptive viewer polling.
- Synchronize the mod, workspaces, lock metadata and exact server client gate
  to beta31. Spectator protocol v2 and Debug recording format v2 are unchanged.

## Validation

The regression suites cover the remaining exact PlayerObject hook set,
authorized/waiting visibility ownership, native world/camera ordering,
Follow-Y history, capture, Spider/Teleport, Safe Mode and Placement persistence.
CI checks the actual packaged native detour markers, including absence of
`setVisible`, retained safety boundaries and iOS Debug ON/OFF probe exclusion.
The user reported the diagnostic visibility fix working on their device;
automated compilation/tests do not establish ten-minute Practice stability or
complete Ranked/Spectator device acceptance.
