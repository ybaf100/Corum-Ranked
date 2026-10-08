#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

ROOT = Path.cwd()
TARGET = Path("ranked/apps/server/test/debug-bot.integration.test.ts")
EXPECTED_BLOB = "c7d3196aedb767c5aedd71e2220c57e7be23b1ac"

OLD = '''      // alpha.10+ gives the trailing side the same 10-second LAST ATTEMPT
      // start window even when it has zero Clears. The rated-match helper must
      // let that authoritative window expire before attempting to ready the
      // following round. Otherwise the next ready call correctly returns 409.
      clock.advance(
        document.operational.rules.lastAttemptWindowSeconds * 1_000 +
          ATTEMPT_TRANSPORT_GRACE_MS +
          1,
      );
      await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext);
      // The fake clock jumps across the LAST ATTEMPT transport grace in one step.
      // In production the DebugBotService polls as the bot every tick, which refreshes
      // its heartbeat. Mirror that here so this rating-flow test does not accidentally
      // turn into a reconnect-timeout/forfeit test when transport grace grows.
      await matches.state(creation.matchId, creation.botMatchToken, creation.botContext);
'''

NEW = '''      // alpha.10+ gives the trailing side the same 10-second LAST ATTEMPT
      // start window even when it has zero Clears. The rated-match helper must
      // let that authoritative window expire before attempting to ready the
      // following round. Otherwise the next ready call correctly returns 409.
      //
      // Refresh the synthetic Bot heartbeat BEFORE the fake-clock jump. In
      // production DebugBotService polls as the Bot every tick; refreshing only
      // after the jump is too late because advanceLockedMatch() evaluates the
      // reconnect timeout before state() can write the new heartbeat. On Round 2
      // that race can incorrectly forfeit the Bot and turn an intended player
      // loss into a Player-A win (+MMR).
      await matches.state(creation.matchId, creation.botMatchToken, creation.botContext);
      clock.advance(
        document.operational.rules.lastAttemptWindowSeconds * 1_000 +
          ATTEMPT_TRANSPORT_GRACE_MS +
          1,
      );
      await matches.state(creation.matchId, creation.playerMatchToken, creation.playerContext);
      await matches.state(creation.matchId, creation.botMatchToken, creation.botContext);
'''

def git_blob_sha(data: bytes) -> str:
    return hashlib.sha1(f"blob {len(data)}\0".encode() + data).hexdigest()

def main() -> int:
    path = ROOT / TARGET
    if not path.is_file():
        print(f"[FAIL] missing {TARGET}", file=sys.stderr)
        return 1

    data = path.read_bytes()
    actual = git_blob_sha(data)
    if actual != EXPECTED_BLOB:
        print(
            "[FAIL] unexpected base file.\n"
            f"expected Git blob: {EXPECTED_BLOB}\n"
            f"actual Git blob:   {actual}\n"
            "This hotfix targets main commit d48f760 (hotfix 5).",
            file=sys.stderr,
        )
        return 1

    text = data.decode("utf-8")
    if text.count(OLD) != 1:
        print("[FAIL] expected test anchor was not found exactly once.", file=sys.stderr)
        return 1

    if "--check" in sys.argv:
        print("[OK] hotfix 5 base verified; validate hotfix is applicable.")
        return 0

    patched = text.replace(OLD, NEW, 1)
    tmp = path.with_name(path.name + ".hotfix6.tmp")
    tmp.write_text(patched, encoding="utf-8", newline="\n")
    tmp.replace(path)

    print("[OK] alpha.37 hotfix 6 validate fix applied.")
    print(f"[OK] changed: {TARGET}")
    print()
    print("Run:")
    print("  npm test --prefix ranked")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
