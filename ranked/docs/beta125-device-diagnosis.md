# beta.125: device evidence and missing diagnostic boundaries

Reviewed against main `76ee464d5e374c75e1b4bb8037570aef6a533f86` (beta.124).
Attachment sizes/hashes and diagnostic summaries: `beta125-device-evidence.json`.
Raw logs and diagnostic exports are kept outside Git; no credentials are copied.

## Confirmed from the new device evidence

- iOS arm64, GD 2.2081, CBF 1.5.0, Geode 5.11.2, Ranked beta.124.
- Three independent replay diagnostic exports stop at frame/step zero with
  `CBF_MAP_OBJECT_IDENTITY_MISMATCH`, no consumed input and no verified frames.
  Levels 48561623, 43115881 and 88103903. These are not motion divergence
  observations. All three exports have empty expected/actual objects.
- The reset error dispatcher only included initial evidence for `CBF_INITIAL*`;
  map mismatch therefore discarded both manifests. That diagnostic bug is fixed.
  The actual source/current map differences cannot be recovered from these files.
- A separate Source capture stops at `CBF_COLLISION_IDENTITY_UNPROVEN`:
  runtimeId 322 is a collision candidate and its directional dictionary key is
  observed. Candidates 322/323 both have type468 and origin195/419.25. This
  does not establish full descriptor equality or target equivalence. The policy
  also rejects duplicate definitions, so the new diagnostic reports its target
  index, descriptor and ambiguity explicitly. The refusal is not bypassed.
- Entropie source Attempt `dfdaec41-2da8-4689-bdfe-b812489c2cfe` reaches
  sequence1435 at 71295 ms. At 19:46:37 the source trace records
  SET_POSITION_OBSERVATION_ONLY from (25891.88,3969) to (25891.88,5437.5),
  teleport flag true before and after. GJ_UPDATE then ends at (25894.281,5439),
  flag false. No PORTAL or CAPTURE event was recorded for that movement.
  DEBUG_CAPTURED and DEBUG_ENCODED_UPLOAD already say p1=none/p2=none;
  RECEIVED_DECODED also has none, and TEST rejects sequence1435 at 19:48:13.
  Thus evidence is absent before upload; a relay-only explanation is insufficient.
  The exact Native mover and whether an entry hook is bypassed/inlined remain
  unproven. A transient flag within a world update is observed, not a new trusted
  teleport event. The existing false-to-true PlayerObject::update condition cannot
  see a flag that is raised and cleared entirely elsewhere in the world update.

## Confirmed load-option loss in code

The Source logs show LDM=true before/after Native init (Purpose at19:41:01,
School at19:42:00 and CASEC at19:43:06).
RankedRoundPreparation::pump applies its default m_lowDetail=false on the
replay path. The initial replay record did not store the actual load options,
so Native replay construction could use a different object set. This missing
state is a confirmed code defect; it is not yet proof that it caused all three
manifest mismatches. New recordings store worldOptions (actual Native LDM and
shake flags). Replay/live-world Native init/reset scopes apply those flags and
restore the shared level preferences afterward. The loaded LDM is checked
before any replay physics. Old recordings carry no LDM evidence: no automatic
ON/OFF inference or manifest bypass is introduced.

## Changes and preserved boundaries

- Report reset map mismatch with recorded/current `cbfInitial` and MAP_IDENTITY
  category. New initial records add diagnostic-only field hashes, at most32
  ordered chunk hashes, at most8 descriptor samples (512-byte sample limit).
  They are computed once per Source Attempt/owned reset, cached across frames,
  and excluded from the authoritative manifest. Old files remain readable.
- Observe collisionCheckObjects, getPortalTargetPos and playerTeleported (the
  latter two are Native hooks only outside Windows, whose bindings are inline),
  and checkCameraLimitAfterTeleport. Record flag transitions as critical within
  the existing160 ordinary/32 critical per-Attempt Entropie trace budget.
- None of these helpers or setPosition creates a trusted movement event.
  All pose/CBF/manifest/actor/direction/dictionary checks remain unchanged.
  No replay rewrite, Best removal, DB migration, scoring or account changes.

## Next device evidence

Run beta.125 and preserve old Bests. A newly recorded Source plus its full replay
file is needed for source field/chunk comparison; an old beta.124 diagnostic
export has no source descriptor diagnostics to reconstruct. Reopen each old
replay to obtain the missing expected/current manifests. Export the new
replays/diagnostics JSON and Geode log. For Entropie record through70–73s and
compare the helper/collision/flag order with CAPTURE, upload and TEST. Do not mark
Native playback/Entropie fixed until an actual device run establishes it.

## Local verification before publication

- Full Native sweep:327/329 initially passed. The two failures were the explicit
  hook allowlist and a Native double missing the newly observed notification.
  Both fixtures were updated, with no visibility hook or gameplay guard removed.
- Final replay/visibility/world-driver target run:15/15 passed, including the new
  production-init option test. Debug OFF/ON capture target run:3/3 passed.
- The same option test run against beta.124's actual init body fails the loaded
  LDM/shake/object-count assertion; the final init passes. These use Native
  doubles, not actual GD collision/teleport binaries.
- Translation catalog check:750 keys/48 achievements unchanged. Packaging tests:
  ModReleaseBuild2/2, ModReleasePublish10/10; production/publication guards10/10.
- Full CI/platform builds and public deployment are separate release gates.
  No actual beta.125 GD/CBF execution has been performed in this environment.
