#!/usr/bin/env python3
from __future__ import annotations

import hashlib
import sys
from pathlib import Path

ROOT = Path.cwd()
TARGET = Path("corum-ranked-mod/src/LevelInfoLayer.cpp")
EXPECTED_BLOB = "4256e642eea443633f13fcfe167ae0fd66088b04"

OLD_LAUNCH = '''        // Latch before vanilla starts mutating/pushing the gameplay scene. A
        // scheduled callback and user Space/Play can otherwise fire in adjacent
        // frames and enter LevelInfoLayer::onPlay twice.
        m_fields->gameplayLaunchStarted = true;
        m_fields->autoPlayScheduled = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked));

        // Only after the authoritative gameplay context is armed do we tear down
        // the song/download gate. This preserves the retry loop until launch is
        // guaranteed to proceed.
        if (m_fields->songGateActive) teardownSongDownloadGate();

        LevelInfoLayer::onPlay(sender);
        return true;
    }

    void scheduleAutoPlay(float delay) {
'''

NEW_LAUNCH = '''        // Latch before vanilla starts mutating/pushing the gameplay scene. A
        // scheduled callback and user Space/Play can otherwise fire in adjacent
        // frames and enter LevelInfoLayer::onPlay twice.
        m_fields->gameplayLaunchStarted = true;
        m_fields->autoPlayScheduled = false;
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::autoPlayRanked));

        // Do NOT tear down the Ranked gate before vanilla actually leaves this
        // LevelInfoLayer. alpha.37 previously treated the call itself as success:
        // if vanilla onPlay() declined/failed to transition, gameplayLaunchStarted
        // stayed latched forever while the gate had already been removed. That
        // exact state leaves every ordinary LevelInfo control usable except Play
        // and Back, which are intercepted by this hook.
        //
        // A successful scene transition calls onExit(), which cancels this
        // watchdog and tears down the gate. If onExit() never happens, we are
        // still on this page; release the latch and retry instead of deadlocking.
        scheduleOnce(
            schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch),
            0.75f
        );

        LevelInfoLayer::onPlay(sender);
        return true;
    }

    void verifyGameplayLaunch(float) {
        if (!m_fields->gameplayLaunchStarted) return;

        log::warn(
            "Ranked LevelInfoLayer::onPlay did not leave the scene; releasing launch latch for retry"
        );

        m_fields->gameplayLaunchStarted = false;
        m_fields->songGateStartingPlay = false;

        if (m_fields->songGateActive) {
            if (m_fields->gateCountdown) {
                m_fields->gateCountdown->setString("PLAY FAILED - RETRYING");
                m_fields->gateCountdown->setColor({255, 92, 92});
            }
            // updateSongDownloadGate() is still alive and will retry on its next
            // tick once the launch latch is released.
            return;
        }

        // This path covers a LevelInfo page that was entered after authority had
        // already moved to PLAYING and therefore had no download gate.
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            isCurrentRankedLevel(m_level) &&
            rankedPlayingState(runtime.view().match.state) &&
            runtime.canEnterCurrentLevel()
        ) {
            scheduleAutoPlay(0.10f);
        }
    }

    void onExit() override {
        // Reaching onExit is the actual proof that vanilla accepted the launch
        // and this LevelInfo scene is leaving. Only now may we destroy the gate.
        unschedule(schedule_selector(CorumRankedLevelInfoLayer::verifyGameplayLaunch));
        if (m_fields->gameplayLaunchStarted && m_fields->songGateActive) {
            teardownSongDownloadGate();
        }
        LevelInfoLayer::onExit();
    }

    void scheduleAutoPlay(float delay) {
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
            "This hotfix targets main commit ed9e467 (hotfix 6).",
            file=sys.stderr,
        )
        return 1

    text = data.decode("utf-8")
    if text.count(OLD_LAUNCH) != 1:
        print("[FAIL] launch anchor was not found exactly once.", file=sys.stderr)
        return 1

    patched = text.replace(OLD_LAUNCH, NEW_LAUNCH, 1)

    required = [
        "verifyGameplayLaunch",
        "void onExit() override",
        "PLAY FAILED - RETRYING",
        "if (m_fields->gameplayLaunchStarted && m_fields->songGateActive)",
    ]
    for marker in required:
        if marker not in patched:
            print(f"[FAIL] transformed source missing {marker!r}", file=sys.stderr)
            return 1

    if "--check" in sys.argv:
        print("[OK] hotfix 6 base verified; launch-deadlock hotfix is applicable.")
        return 0

    tmp = path.with_name(path.name + ".hotfix7.tmp")
    tmp.write_text(patched, encoding="utf-8", newline="\n")
    tmp.replace(path)

    print("[OK] Corum Ranked v0.4.0-alpha.37 hotfix 7 applied.")
    print(f"[OK] changed: {TARGET}")
    print()
    print("Rebuild the Geode mod after applying this hotfix.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
