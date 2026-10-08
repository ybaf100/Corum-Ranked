# Android build notes

Corum Integration v1.0.2 keeps End Screen capture inside the Geometry Dash
Cocos renderer. It does not use Android `MediaProjection`.

## Local Geode CLI build

Install the Geode CLI/SDK and an Android NDK first, set `ANDROID_NDK_ROOT`, then
run these commands from `corum-integration-mod`:

```bash
geode build -p android32
geode build -p android64
```

The equivalent long option is `--platform`.

## GitHub Actions build

`.github/workflows/build-mod.yml` follows Geode's multi-platform build pattern.
It builds Win64, iOS arm64, Android32, and Android64 independently and uses
Geode's combine action to produce one `v1.0.2` package containing all four
platform binaries.

## Device validation checklist

1. Launch Geometry Dash with Geode 5.8.2 and Corum Integration enabled.
2. Confirm the startup catalog completes normally.
3. Complete a Corum-listed map in Normal Mode outside Test Mode.
4. Confirm there is no Android screen-recording / screen-sharing consent dialog.
5. Confirm a lossless PNG is staged in the mod's save directory and no evidence
   request is made at clear time.
6. Confirm the captured PNG is filled by the game view rather than containing a
   tiny bottom-left game image surrounded by black space.
7. Confirm the End Level dialog has finished its entrance animation before the
   stored frame is captured.
8. Open Submit for the cleared map and confirm the pending PNG is uploaded only
   after pressing Submit.
9. While that evidence upload is active, complete a different Corum map and
   confirm both maps keep independent pending captures.
10. Confirm successful record submission deletes only that map's local pending PNG and a
   later re-clear does not create another capture for the same evidence generation.
11. Repeat with a primary/alternate-map pair and confirm both share one canonical
   pending evidence slot.
12. Clear a Corum map, wait for the End Screen capture to finish, restart the
    game, then submit and confirm the staged PNG is uploaded and linked to the
    record.
13. Before submitting an unverified capture, press Replay and clear the same
    map again. Confirm the newer completion replaces that map's pending PNG.
14. Press Replay before the 0.80-second capture delay finishes, clear the map
    again, and confirm only the later completed End Screen is stored.
15. Delete only that pending PNG while leaving saved metadata, press Submit,
    and confirm the record request continues without local evidence.
