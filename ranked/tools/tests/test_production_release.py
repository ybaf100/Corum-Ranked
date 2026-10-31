import copy
import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("production", Path(__file__).parents[1] / "verify-production-release.py")
production = importlib.util.module_from_spec(spec)
spec.loader.exec_module(production)
VERSION = "v0.5.0-beta.106"
BASE = "https://cdn.test"


class ProductionReleaseTest(unittest.TestCase):
    def setUp(self):
        self.packages = {channel: {"channel": channel, "version": VERSION, "sha256": channel * 8, "sizeBytes": 123}
                         for channel in ("release", "debug")}
        self.health = {"status": "ok", "service": "corum-ranked-server"}
        self.ready = {key: True for key in ("ready", "databaseConnected", "databaseReady", "schemaReady", "rankedReady")}
        self.ready["config"] = {"ready": True, "queueEnabled": True}
        self.config = {"requiredClientVersion": VERSION, "modUpdates": copy.deepcopy(self.packages)}
        for channel, update in self.config["modUpdates"].items():
            update["downloadUrl"] = f"{BASE}/ranked/{channel}-{VERSION}/Corum-Ranked-{VERSION}-{channel}.geode"

    def verify(self, expected_sha=None):
        with patch.object(production.clients, "wait_for_clients", return_value=self.packages), \
             patch.object(production, "read_json", side_effect=[self.health, self.ready, self.config]):
            return production.verify(VERSION, BASE, expected_sha)

    def test_both_public_channels_and_readiness_must_match(self):
        self.assertTrue(self.verify()["ready"])

    def test_operator_queue_off_is_preserved(self):
        self.ready["rankedReady"] = False
        self.ready["config"]["queueEnabled"] = False
        self.assertTrue(self.verify()["ready"])

    def test_active_server_source_must_equal_the_verified_client_source(self):
        self.health['sourceSha'] = 'a'*40
        self.assertEqual(self.verify('a'*40)['sourceSha'], 'a'*40)
        for source in ('b'*40, None):
            self.health['sourceSha'] = source
            with self.assertRaisesRegex(ValueError, 'Active server source SHA'):
                self.verify('a'*40)

    def test_bad_readiness_version_hash_or_url_fails(self):
        cases = [("ready", "schemaReady", False), ("ready", "rankedReady", False),
                 ("config", "requiredClientVersion", "old")]
        for target, key, value in cases:
            with self.subTest(target=target, key=key):
                original = getattr(self, target)[key]
                getattr(self, target)[key] = value
                with self.assertRaises(ValueError):
                    self.verify()
                getattr(self, target)[key] = original
        for channel in ("release", "debug"):
            for key in ("version", "sha256", "sizeBytes", "downloadUrl"):
                with self.subTest(channel=channel, key=key):
                    update = self.config["modUpdates"][channel]
                    original = update[key]
                    update[key] = "wrong"
                    with self.assertRaises(ValueError):
                        self.verify()
                    update[key] = original


if __name__ == "__main__":
    unittest.main()
