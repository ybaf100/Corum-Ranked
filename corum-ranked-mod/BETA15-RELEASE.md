# Corum Ranked v0.5.0-beta.15

## Gameplay UX

- Death Match shows local and opponent Geometry Dash icons on the vanilla progress bar using live progress.
- Death Match moves the local remaining Attempt budget to the upper-left HUD and uses the former center counter position for the live progress difference: positive is green, negative is red, zero is gray.
- Standard Ranked rounds show the live score difference directly below the vanilla percentage label.

## Practice / spectator isolation

- User Practice Mode remains available in normal Ranked rounds and Placement, but is disabled during `DEATHMATCH_PLAYING`.
- Live spectator frames expose vanilla Practice semantics only while the original `PlayLayer::postUpdate` executes. The Practice flag is restored before Ranked state/timeout handling resumes, so spectator read-only mode cannot be mistaken for user Practice or trigger Practice-based local exit logic.
- Spectator Attempt transport remains suppressed by the existing read-only spectator lifecycle.

## CI policy

- Routine pushes use the Debug workflow only for native Geode builds.
- The Release workflow is manual-only.
- The legacy CI Log Relay workflow was removed because GitHub Actions logs are now read directly and the relay consumed an extra runner.
