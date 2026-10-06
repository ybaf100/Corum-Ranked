"""Stage 2 matrix and packaged native detour verification; no device claims."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import zipfile

HERE = Path(__file__).resolve().parent
SELECTION = json.loads((HERE / "player-hook-stage2.json").read_text())
INVENTORY = json.loads((HERE / "hook-inventory-beta30.json").read_text())


def matrix():
    return {"include": [dict(
        variant=v["variant"], debug=v["debug"], diagnostic=v["diagnostic"],
        exclusions=" ".join(f"-D{flag}=ON" for flag in v["exclusions"]),
    ) for v in SELECTION["variants"]]}


def expected_targets(variant):
    selected = set(variant["enabledPlayerHooks"])
    return {f'{h["targetClass"]}::{h["function"]}' for h in INVENTORY["hooks"]
            if h["iosInstalled"]
            and (not h["debugOnly"] or variant["debug"] == "ON")
            and (h["group"] != "A" or "CORUM_CRASH_DIAG_NO_PROBES_HOOKS" not in variant["exclusions"])
            and (h["group"] != "B" or h["function"] in selected)}


def verify_binary(binary, variant):
    # Full SDK markers distinguish registered modify declarations from imported
    # binding names and from the diagnostic expectation strings themselves.
    actual = {m.decode() for m in re.findall(
        rb"\[GEODE_MODIFY_NAME\] ([^\s]+) \[GEODE_MODIFY_ADDRESS\] ", binary)}
    expected = expected_targets(variant)
    assert actual == expected, {"missing": sorted(expected - actual), "unexpected": sorted(actual - expected)}
    diagnostic = b"[CorumCrashDiag]" in binary
    assert diagnostic == (variant["diagnostic"] == "ON"), "diagnostic code default exclusion mismatch"
    # Thin iOS arm64 Mach-O, matching the Stage 1 build target.
    assert struct.unpack_from("<II", binary) == (0xFEEDFACF, 0x0100000C), "expected iOS arm64 dylib"
    return sorted(actual)


def verify_package(output, variant):
    packages = list(output.glob("*.geode"))
    assert len(packages) == 1, packages
    original = packages[0]
    with zipfile.ZipFile(original) as package:
        manifest = json.loads(package.read("mod.json"))
        assert manifest["id"] == "hwanhee1.corum_ranked"
        assert manifest["version"] == SELECTION["version"]
        native = [name for name in package.namelist() if name.endswith(".ios.dylib")]
        assert len(native) == 1, native
        markers = verify_binary(package.read(native[0]), variant)
    target = output / f'{variant["variant"]}.geode'
    original.rename(target)
    enabled = [f"PlayerObject::{name}" for name in variant["enabledPlayerHooks"]]
    disabled = [f'PlayerObject::{h["function"]}' for h in SELECTION["playerHooks"]
                if h["function"] not in variant["enabledPlayerHooks"]]
    provenance = {
        "variant": variant["variant"], "commit": os.environ["GITHUB_SHA"],
        "baseline": SELECTION["baseline"], "diagnosticStart": SELECTION["diagnosticStart"],
        "version": manifest["version"], "geodeSDK": "5.8.2", "deviceGeodeRuntime": "5.10.1",
        "bindings": INVENTORY["bindings"], "tulipRuntime": "3.1.14",
        "spectatorProtocol": 2, "debugRecordingFormat": 2,
        "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
        "compileDefinitions": variant["exclusions"],
        "diagnosticCompiledIn": variant["diagnostic"] == "ON", "debug": variant["debug"],
        "nativeProbesExcluded": "GJBaseGameLayer::checkCollisions" not in markers,
        "enabledPlayerHooks": enabled, "disabledPlayerHooks": disabled,
        "verifiedNativeModifyTargets": markers,
        "nativeHookExclusionMarkersVerified": True,
        "startupRegistryCheck": "require player-registry-match=true and priorityFailures=0 on device"
            if variant["diagnostic"] == "ON" else "not compiled in; production-default compile check",
        "diagnosticOnly": True, "deviceVerified": False,
        "purpose": "ordinary Practice isolation; excluded safety hooks are unsuitable for Ranked/Spectator",
    }
    (output / "diagnostic-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    print(json.dumps({"variant": variant["variant"], "enabledPlayerHooks": enabled,
                      "disabledPlayerHooks": disabled, "verifiedNativeTargets": len(markers)}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix", action="store_true")
    parser.add_argument("--variant")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.matrix:
        print("matrix=" + json.dumps(matrix(), separators=(",", ":")))
    else:
        assert args.variant and args.output
        variants = [v for v in SELECTION["variants"] if v["variant"] == args.variant]
        assert len(variants) == 1, args.variant
        verify_package(args.output, variants[0])
