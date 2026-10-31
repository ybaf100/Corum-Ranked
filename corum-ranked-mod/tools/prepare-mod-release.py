#!/usr/bin/env python3
"""Stamp a combined package and emit an immutable channel-specific release."""
import argparse
import hashlib
import json
import os
import re
import zipfile
from pathlib import Path
from urllib.parse import urlsplit

MOD_ID = "hwanhee1.corum_ranked"
BINARIES = {MOD_ID + extension for extension in (".dll", ".dylib", ".ios.dylib", ".android32.so", ".android64.so")}
MARKER = "corum-build-channel.json"


def prepare(package: Path, channel: str, version: str, output: Path, source_sha=None):
    if channel not in ("debug", "release") or not re.fullmatch(r"v\d+\.\d+\.\d+(?:-[a-zA-Z0-9.]+)?", version):
        raise ValueError("Invalid release channel/version")
    if source_sha is not None and not re.fullmatch(r"[0-9a-f]{40}", source_sha):
        raise ValueError("Invalid source SHA")
    output.mkdir(parents=True, exist_ok=True)
    filename = f"Corum-Ranked-{version}-{channel}.geode"
    destination = output / filename
    temporary = destination.with_suffix(".tmp")
    with zipfile.ZipFile(package) as source:
        names = source.namelist()
        if len(names) != len(set(names)):
            raise ValueError("Duplicate package entries")
        manifest = json.loads(source.read("mod.json"))
        if manifest.get("id") != MOD_ID or manifest.get("version") != version or not BINARIES <= set(names):
            raise ValueError("Wrong mod identity/version or missing native platform")
        with zipfile.ZipFile(temporary, "w", compression=zipfile.ZIP_DEFLATED) as stamped:
            for entry in source.infolist():
                if entry.filename != MARKER:
                    stamped.writestr(entry, source.read(entry))
            marker = zipfile.ZipInfo(MARKER, (1980, 1, 1, 0, 0, 0))
            marker.compress_type = zipfile.ZIP_DEFLATED
            stamp = {"channel": channel, "version": version}
            if source_sha is not None:
                stamp["sourceSha"] = source_sha
            stamped.writestr(marker, json.dumps(stamp, separators=(",", ":")))
    temporary.replace(destination)
    size = destination.stat().st_size
    if not 0 < size <= 128 * 1024 * 1024:
        raise ValueError("Mod update exceeds the client download limit")
    descriptor = {"version": version, "sha256": hashlib.sha256(destination.read_bytes()).hexdigest(), "sizeBytes": size}
    if source_sha is not None:
        descriptor["sourceSha"] = source_sha
    base = os.environ.get("RANKED_DOWNLOAD_BASE_URL", "").strip().rstrip("/")
    if base:
        parsed = urlsplit(base)
        if parsed.scheme != "https" or not parsed.hostname or parsed.username or parsed.password or parsed.path or parsed.query or parsed.fragment:
            raise ValueError("RANKED_DOWNLOAD_BASE_URL must be a public HTTPS bucket origin")
        descriptor["downloadUrl"] = f"{base}/ranked/{channel}-{version}/{filename}"
    (output / f"{channel}.json").write_text(json.dumps({channel: descriptor}, indent=2) + "\n")
    return destination


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("package", type=Path)
    parser.add_argument("channel", choices=("debug", "release"))
    parser.add_argument("version")
    parser.add_argument("output", type=Path)
    parser.add_argument("--source-sha")
    args = parser.parse_args()
    print(prepare(args.package, args.channel, args.version, args.output, args.source_sha))
