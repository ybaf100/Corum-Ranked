"""CI-only release barrier. Never contacts Postgres or changes production state."""
import argparse
import hashlib
import io
import json
import re
import time
import urllib.error
import urllib.request
import zipfile

PLATFORMS = {"hwanhee1.corum_ranked.dll", "hwanhee1.corum_ranked.dylib", "hwanhee1.corum_ranked.ios.dylib",
             "hwanhee1.corum_ranked.android32.so", "hwanhee1.corum_ranked.android64.so"}


def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "Corum-release-barrier"})
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read(128 * 1024 * 1024 + 1)
    if len(data) > 128 * 1024 * 1024:
        raise ValueError("Published client exceeds download limit")
    return data


def verify_channel(base, version, channel, expected_sha=None):
    prefix = f"{base.rstrip('/')}/ranked/{channel}-{version}"
    descriptor = json.loads(fetch(f"{prefix}/{channel}.json"))[channel]
    if descriptor.get("version") != version:
        raise ValueError(f"{channel}: descriptor version mismatch")
    data = fetch(f"{prefix}/Corum-Ranked-{version}-{channel}.geode")
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != descriptor.get("sizeBytes") or digest != descriptor.get("sha256"):
        raise ValueError(f"{channel}: package size/hash mismatch")
    with zipfile.ZipFile(io.BytesIO(data)) as package:
        manifest = json.loads(package.read("mod.json"))
        stamp = json.loads(package.read("corum-build-channel.json"))
        if manifest.get("id") != "hwanhee1.corum_ranked" or manifest.get("version") != version:
            raise ValueError(f"{channel}: embedded mod identity/version mismatch")
        expected_stamp = {"channel": channel, "version": version}
        source_sha = stamp.get("sourceSha")
        if source_sha is not None:
            if not isinstance(source_sha, str) or not re.fullmatch(r"[0-9a-f]{40}", source_sha):
                raise ValueError(f"{channel}: invalid source SHA")
            expected_stamp["sourceSha"] = source_sha
        if descriptor.get("sourceSha") != source_sha or (expected_sha is not None and source_sha != expected_sha):
            raise ValueError(f"{channel}: source SHA mismatch")
        if stamp != expected_stamp:
            raise ValueError(f"{channel}: embedded channel mismatch")
        if not PLATFORMS.issubset(package.namelist()):
            raise ValueError(f"{channel}: platform binary missing")
    result = {"channel": channel, "version": version, "sizeBytes": len(data), "sha256": digest}
    if source_sha is not None:
        result["sourceSha"] = source_sha
    return result


def wait_for_clients(base, version, timeout, expected_sha=None):
    if not base.startswith("https://") or not version.startswith("v") or "/" in version:
        raise ValueError("Expected HTTPS download base and exact immutable mod version")
    if expected_sha is not None and not re.fullmatch(r"[0-9a-f]{40}", expected_sha):
        raise ValueError("Expected exact source SHA")
    deadline = time.monotonic() + timeout
    verified = {}
    while len(verified) < 2:
        for channel in ("release", "debug"):
            if channel in verified:
                continue
            try:
                verified[channel] = verify_channel(base, version, channel, expected_sha) if expected_sha is not None else verify_channel(base, version, channel)
                print(json.dumps(verified[channel]), flush=True)
            except urllib.error.HTTPError as error:
                if error.code != 404 and error.code < 500:
                    raise
            except (urllib.error.URLError, TimeoutError):
                pass
        if len(verified) == 2:
            return verified
        if time.monotonic() >= deadline:
            raise TimeoutError("Both verified client channels are required before activation")
        time.sleep(5)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--wait-seconds", type=int, default=1200)
    parser.add_argument("--expected-sha")
    args = parser.parse_args()
    wait_for_clients(args.base, args.version, args.wait_seconds, args.expected_sha)
