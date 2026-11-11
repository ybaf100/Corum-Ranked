"""Read-only verification after the single-run Singapore release activation."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
API = "https://api.52-74-37-130.sslip.io"
spec = importlib.util.spec_from_file_location("published_clients", Path(__file__).with_name("verify-published-clients.py"))
clients = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clients)


def read_json(path):
    request = urllib.request.Request(API + path, headers={"User-Agent": "Corum-release-verification"})
    with urllib.request.urlopen(request, timeout=30) as response:
        if response.status != 200:
            raise ValueError(f"{path}: HTTP {response.status}")
        return json.load(response)


def verify(version, base, expected_sha=None, server_sha=None, client_sha=None):
    # expected_sha pins both the server and the published clients to one
    # source. A server-only release keeps the clients of an earlier source:
    # then only server_sha is pinned and the clients are verified by bytes.
    server_sha = server_sha or expected_sha
    client_sha = expected_sha if client_sha is None else (client_sha or None)
    packages = clients.wait_for_clients(base, version, 0, client_sha) if client_sha else clients.wait_for_clients(base, version, 0)
    health = read_json("/health")
    if health.get("status") != "ok" or health.get("service") != "corum-ranked-server":
        raise ValueError("Singapore health failed")
    if server_sha is not None and health.get("sourceSha") != server_sha:
        raise ValueError("Active server source SHA differs from the verified clients")
    ready = read_json("/ready")
    if not all(ready.get(key) is True for key in ("ready", "databaseConnected", "databaseReady", "schemaReady")):
        raise ValueError("Singapore database/schema readiness failed")
    if ready.get("config", {}).get("ready") is not True:
        raise ValueError("Singapore config is not ready")
    # Queue OFF is a supported operator setting, not a deployment failure.
    if ready.get("rankedReady") is not True and ready.get("config", {}).get("queueEnabled") is not False:
        raise ValueError("Ranked unavailable without an intentional queue OFF")
    config = read_json("/api/ranked/config")
    if config.get("requiredClientVersion") != version:
        raise ValueError("Required client version does not match the release")
    for channel, package in packages.items():
        update = config.get("modUpdates", {}).get(channel, {})
        if any(update.get(key) != package[key] for key in ("version", "sizeBytes", "sha256")):
            raise ValueError(f"{channel}: live API does not match verified public bytes")
        expected = f"{base.rstrip('/')}/ranked/{channel}-{version}/Corum-Ranked-{version}-{channel}.geode"
        if update.get("downloadUrl") != expected:
            raise ValueError(f"{channel}: unexpected live download URL")
    result = {"api": API, "version": version, "health": True, "ready": True,
              "clients": packages, "modes": config.get("modes", {})}
    if server_sha is not None:
        result["sourceSha"] = health["sourceSha"]
    print(json.dumps(result), flush=True)
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-sha")
    parser.add_argument("--server-sha")
    parser.add_argument("--client-sha", help="empty: clients published by an earlier source")
    args = parser.parse_args()
    version = json.loads((ROOT / "corum-ranked-mod/mod.json").read_text())["version"]
    verify(version, os.environ.get("RANKED_DOWNLOAD_BASE_URL") or
           "https://pub-11442f32945446a49b9da0a5e69f2451.r2.dev", args.expected_sha, args.server_sha, args.client_sha)
