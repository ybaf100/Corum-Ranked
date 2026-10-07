"""Verify compiled detours in a real Geode package; no device stability claim."""
import argparse
import json
from pathlib import Path
import re
import struct
import zipfile


def verify_binary(binary, platform, debug):
    targets = {name.decode() for name in re.findall(
        rb"\[GEODE_MODIFY_NAME\] ([^\s]+) \[GEODE_MODIFY_ADDRESS\] ", binary)}
    expected = {"PlayerObject::" + name for name in
                ("update", "updateRotation", "spiderTestJump", "setPosition", "setRotation")}
    if platform != "win":
        expected.add("PlayerObject::updateSpecial")
    player = {name for name in targets if name.startswith("PlayerObject::")}
    if player != expected:
        raise ValueError({"platform": platform, "missing": sorted(expected - player),
                          "unexpected": sorted(player - expected)})
    required = {"GJGameLevel::savePercentage", "GJGameLevel::storeNewLocalScore",
                "PlayLayer::destroyPlayer", "PlayLayer::levelComplete",
                "PauseLayer::customSetup",
                "GameManager::returnToLastScene",
                "GJBaseGameLayer::update", "GJBaseGameLayer::updateCamera",
                "GJBaseGameLayer::teleportPlayer"}
    if not required <= targets or "GJGameLevel::saveNewScore" in targets:
        raise ValueError("missing safety/capture boundary or unsafe persistence detour")
    if b"[CorumCrashDiag]" in binary:
        raise ValueError("diagnostic runtime must not enter production packages")
    if platform == "ios":
        if struct.unpack_from("<II", binary) != (0xFEEDFACF, 0x0100000C):
            raise ValueError("expected a thin iOS arm64 dylib")
        probes = {"GJBaseGameLayer::checkCollisions", "GJBaseGameLayer::collisionCheckObjects",
                  "GJBaseGameLayer::playerTouchedTrigger", "EffectGameObject::triggerObject"}
        if targets & probes != (probes if debug else set()):
            raise ValueError("Debug ON/OFF native instrumentation mismatch")
    return sorted(player)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("version")
    parser.add_argument("--debug", action="store_true")
    parser.add_argument("--ios-only", action="store_true")
    args = parser.parse_args()
    if args.package.is_dir():
        packages = list(args.package.glob("*.geode"))
        if len(packages) != 1:
            raise ValueError(f"expected one Geode package, found {len(packages)}")
        args.package = packages[0]
    binaries = {"ios": "hwanhee1.corum_ranked.ios.dylib"}
    if not args.ios_only:
        binaries.update(win="hwanhee1.corum_ranked.dll",
                        android32="hwanhee1.corum_ranked.android32.so",
                        android64="hwanhee1.corum_ranked.android64.so")
    with zipfile.ZipFile(args.package) as package:
        manifest = json.loads(package.read("mod.json"))
        if manifest.get("id") != "hwanhee1.corum_ranked" or manifest.get("version") != args.version:
            raise ValueError("packaged mod identity/version mismatch")
        for platform, name in binaries.items():
            player = verify_binary(package.read(name), platform, args.debug)
            print(json.dumps({"platform": platform, "debug": args.debug, "playerHooks": player}))
