#!/usr/bin/env python3
"""Measure public package URLs without changing R2, descriptors or installations."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import time
from urllib.parse import urlsplit

MAX_BYTES = 128 * 1024 * 1024


def public_url(text):
    url = urlsplit(text)
    if (url.scheme != "https" or not url.hostname or url.username or url.password or
            url.query or url.fragment or any(ord(c) <= 32 for c in text)):
        raise ValueError("Use a public HTTPS package URL without credentials, query or fragment")
    return text


def response_headers(text):
    # Ignore CONNECT and redirect headers; report the final HTTP response.
    result = {}
    for line in text.splitlines():
        if line.startswith("HTTP/"):
            result = {}
        elif ":" in line:
            key, value = line.split(":", 1)
            result[key.lower().strip()] = value.strip()
    return result


def probe(url, timeout=180):
    public_url(url)
    with tempfile.TemporaryDirectory(prefix="corum-download-probe-") as directory:
        package, headers = Path(directory) / "package.geode", Path(directory) / "headers.txt"
        result = subprocess.run(["curl", "--silent", "--show-error", "--fail", "--location",
            "--proto", "=https", "--proto-redir", "=https", "--max-time", str(timeout),
            "--max-filesize", str(MAX_BYTES), "--dump-header", str(headers),
            "--output", str(package), "--write-out", "%{json}", url],
            capture_output=True, text=True)
        metrics = json.loads(result.stdout) if result.stdout.strip() else {}
        head = response_headers(headers.read_text()) if headers.exists() else {}
        report = {"url": url, "effectiveUrl": metrics.get("url_effective"), "exitCode": result.returncode,
            "status": metrics.get("http_code"), "bytes": metrics.get("size_download", 0),
            "connectSeconds": metrics.get("time_connect"), "firstResponseSeconds": metrics.get("time_starttransfer"),
            "totalSeconds": metrics.get("time_total"), "MBps": metrics.get("speed_download", 0) / 1e6,
            "redirects": metrics.get("num_redirects"), "headers": {key: head.get(key) for key in (
                "content-length", "content-type", "cache-control", "cf-cache-status", "age", "cf-ray", "server")}}
        if result.returncode:
            report["error"] = metrics.get("errormsg") or result.stderr.strip()
            return report
        start = time.perf_counter()
        sha = hashlib.sha256()
        with package.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                sha.update(chunk)
        report.update(sha256=sha.hexdigest(), sha256Ms=(time.perf_counter() - start) * 1000)
        return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("urls", nargs="+", type=public_url)
    parser.add_argument("--samples", type=int, choices=range(1, 6), default=2)
    parser.add_argument("--timeout", type=int, choices=range(1, 601), default=180)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    samples = [probe(url, args.timeout) for url in args.urls for _ in range(args.samples)]
    report = {"network": "This machine, including its configured proxy; not a device measurement", "samples": samples}
    digests = {s["sha256"] for s in samples if "sha256" in s}
    report["sameBytes"] = len(digests) == 1 and all(s["exitCode"] == 0 for s in samples)
    value = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(value)
    print(value, end="")
    return 0 if all(s["exitCode"] == 0 for s in samples) else 1


if __name__ == "__main__":
    raise SystemExit(main())
