#!/usr/bin/env python3
"""Publish validated, immutable Ranked packages to Cloudflare R2 using S3 SigV4."""
import argparse
import datetime
import hashlib
import hmac
import io
import json
import os
from pathlib import Path
import re
import urllib.error
import urllib.parse
import urllib.request
import zipfile

MAX_PACKAGE_BYTES = 128 * 1024 * 1024
MOD_ID = "hwanhee1.corum_ranked"
NATIVE_BINARIES = {MOD_ID + suffix for suffix in (
    ".dll", ".dylib", ".android64.so", ".android32.so", ".ios.dylib")}


class PublishError(RuntimeError):
    def __init__(self, message, status=None):
        super().__init__(message)
        self.status = status


def public_base(text):
    base = text.strip().rstrip("/")
    parsed = urllib.parse.urlsplit(base)
    if (parsed.scheme != "https" or not parsed.hostname or parsed.username or
            parsed.password or parsed.path or parsed.query or parsed.fragment):
        raise PublishError("RANKED_DOWNLOAD_BASE_URL must be a public HTTPS origin")
    return base


class R2Client:
    def __init__(self, account, access_key, secret_key, bucket):
        if not re.fullmatch(r"[0-9a-fA-F]{32}", account or ""):
            raise PublishError("Configure RANKED_R2_ACCOUNT_ID in Actions variables")
        access_key, secret_key = access_key.strip(), secret_key.strip()
        if not access_key or not secret_key or any(ord(c) <= 32 for c in access_key + secret_key):
            raise PublishError("Configure both R2 credentials in Actions secrets")
        if not re.fullmatch(r"[a-z0-9][a-z0-9.-]{1,61}[a-z0-9]", bucket or ""):
            raise PublishError("Invalid RANKED_R2_BUCKET")
        self.host = account + ".r2.cloudflarestorage.com"
        self.access_key, self.secret_key, self.bucket = access_key, secret_key, bucket

    def send(self, method, key, payload=b"", headers=None, max_bytes=4096):
        stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        day = stamp[:8]
        digest = hashlib.sha256(payload).hexdigest()
        path = "/" + self.bucket + "/" + urllib.parse.quote(key, safe="/-_.~")
        signed = {"host": self.host, "x-amz-date": stamp, "x-amz-content-sha256": digest}
        signed.update({name.lower(): str(value).strip() for name, value in (headers or {}).items()})
        names = ";".join(sorted(signed))
        canonical_headers = "".join(name + ":" + signed[name] + "\n" for name in sorted(signed))
        canonical = "\n".join((method, path, "", canonical_headers, names, digest))
        scope = day + "/auto/s3/aws4_request"
        to_sign = "\n".join(("AWS4-HMAC-SHA256", stamp, scope,
                             hashlib.sha256(canonical.encode()).hexdigest()))
        signing_key = ("AWS4" + self.secret_key).encode()
        for part in (day, "auto", "s3", "aws4_request"):
            signing_key = hmac.new(signing_key, part.encode(), hashlib.sha256).digest()
        signature = hmac.new(signing_key, to_sign.encode(), hashlib.sha256).hexdigest()
        signed["authorization"] = ("AWS4-HMAC-SHA256 Credential=" + self.access_key + "/" + scope +
                                   ", SignedHeaders=" + names + ", Signature=" + signature)
        request = urllib.request.Request("https://" + self.host + path,
                                         data=payload if method == "PUT" else None,
                                         headers=signed, method=method)
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                size = response.headers.get("content-length")
                if size and int(size) > max_bytes:
                    raise PublishError("Existing R2 object exceeds the expected size")
                data = response.read(max_bytes + 1)
                if len(data) > max_bytes:
                    raise PublishError("R2 response exceeded the size limit")
                return data
        except urllib.error.HTTPError as error:
            raise PublishError(f"R2 {method} failed with HTTP {error.code}", error.code) from None
        except urllib.error.URLError:
            raise PublishError("R2 connection failed; check account and bucket configuration") from None

    def put_immutable(self, key, payload, content_type):
        # Refuse known differences before any write as well as protecting the
        # creation race with If-None-Match below.
        try:
            existing = self.send("GET", key, max_bytes=len(payload))
        except PublishError as error:
            if error.status != 404:
                raise
        else:
            if existing != payload:
                raise PublishError("Immutable R2 object has different bytes; use a new version")
            return
        try:
            self.send("PUT", key, payload, {
                "content-type": content_type,
                "cache-control": "public, max-age=31536000, immutable",
                "if-none-match": "*",
                "x-amz-meta-sha256": hashlib.sha256(payload).hexdigest(),
            })
        except PublishError as error:
            if error.status != 412:
                raise
            existing = self.send("GET", key, max_bytes=len(payload))
            if existing != payload:
                raise PublishError("Immutable R2 object has different bytes; use a new version")


def publish(directory, channel, version, base, client, expected_sha=None):
    if channel not in {"debug", "release"} or not re.fullmatch(
            r"v\d+\.\d+\.\d+(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?", version):
        raise PublishError("Invalid Ranked channel or version")
    base = public_base(base)
    package_path = Path(directory) / (MOD_ID + ".geode")
    if not 0 < package_path.stat().st_size <= MAX_PACKAGE_BYTES:
        raise PublishError("Ranked package is empty or exceeds 128 MiB")
    payload = package_path.read_bytes()
    with zipfile.ZipFile(io.BytesIO(payload)) as package:
        files = package.namelist()
        if len(files) != len(set(files)):
            raise PublishError("Ranked package contains duplicate entries")
        if not NATIVE_BINARIES.issubset(files):
            raise PublishError("Ranked package is missing native platform binaries")
        manifest = json.loads(package.read("mod.json"))
        marker = json.loads(package.read("corum-build-channel.json"))
        if not isinstance(manifest, dict) or manifest.get("id") != MOD_ID or manifest.get("version") != version:
            raise PublishError("Ranked package identity or version does not match")
        stamp = {"channel": channel, "version": version}
        source_sha = marker.get("sourceSha")
        if source_sha is not None:
            if not isinstance(source_sha, str) or not re.fullmatch(r"[0-9a-f]{40}", source_sha):
                raise PublishError("Invalid package source SHA")
            stamp["sourceSha"] = source_sha
        if expected_sha is not None and source_sha != expected_sha:
            raise PublishError("Package source SHA differs from the publication run")
        if marker != stamp:
            raise PublishError("Ranked package channel marker does not match")
    descriptor_path = Path(directory) / (channel + ".json")
    if descriptor_path.stat().st_size > 4096:
        raise PublishError("Ranked descriptor exceeds the size limit")
    descriptor = json.loads(descriptor_path.read_text())
    if not isinstance(descriptor, dict) or set(descriptor) != {channel}:
        raise PublishError("Ranked descriptor must contain exactly the selected channel")
    data = descriptor[channel]
    if (not isinstance(data, dict) or data.get("version") != version or
            data.get("sizeBytes") != len(payload) or
            data.get("sha256") != hashlib.sha256(payload).hexdigest()):
        raise PublishError("Ranked descriptor hash, size or version does not match the package")
    if data.get("sourceSha") != source_sha:
        raise PublishError("Descriptor/package source SHA mismatch")
    prefix = "ranked/" + channel + "-" + version
    filename = "Corum-Ranked-" + version + "-" + channel + ".geode"
    url = base + "/" + prefix + "/" + filename
    if data.get("downloadUrl", url) != url:
        raise PublishError("Ranked descriptor points outside the configured R2 package path")
    canonical = {channel: {"version": version, "sha256": data["sha256"],
                           "sizeBytes": len(payload), "downloadUrl": url}}
    if source_sha is not None:
        canonical[channel]["sourceSha"] = source_sha
    descriptor_payload = (json.dumps(canonical, indent=2) + "\n").encode()
    # The public descriptor is the commit point. A failed package upload never
    # advertises an incomplete update; retries cannot replace different bytes.
    client.put_immutable(prefix + "/" + filename, payload, "application/octet-stream")
    client.put_immutable(prefix + "/" + channel + ".json", descriptor_payload, "application/json")
    return url


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("channel", choices=("debug", "release"))
    parser.add_argument("version")
    args = parser.parse_args()
    try:
        client = R2Client(os.environ.get("RANKED_R2_ACCOUNT_ID", ""),
                          os.environ.get("RANKED_R2_ACCESS_KEY_ID", ""),
                          os.environ.get("RANKED_R2_SECRET_ACCESS_KEY", ""),
                          os.environ.get("RANKED_R2_BUCKET", "corum-ranked-download"))
        url = publish(args.directory, args.channel, args.version,
                      os.environ.get("RANKED_DOWNLOAD_BASE_URL", ""), client, os.environ.get("GITHUB_SHA"))
    except (PublishError, OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        message = str(error) if isinstance(error, PublishError) else "Invalid or unreadable Ranked package/configuration"
        print("::error::" + message)
        return 1
    print("Published immutable Ranked package: " + url)
    if os.environ.get("GITHUB_STEP_SUMMARY"):
        with open(os.environ["GITHUB_STEP_SUMMARY"], "a") as summary:
            summary.write("Published Ranked " + args.channel + ": " + url + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
