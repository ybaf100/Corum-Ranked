from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual < count:
        raise SystemExit(f"{path}: expected at least {count} occurrence(s), found {actual}: {old[:80]!r}")
    text = text.replace(old, new, count)
    target.write_text(text, encoding="utf-8")


# Server: keep the visible ten-second PVP countdown separate from the hard
# network/resource deadline. The client itself can spend up to 30 seconds on a
# vanilla map download, so a ten-second server timeout is inherently racy.
replace(
    "ranked/apps/server/src/match/match.service.ts",
    '''// PvP preparation is a real ten-second start countdown. Resource lookup, map
// acquisition, and song acquisition begin immediately inside that window; an
// old alpha-era 60-second safety floor must not extend the visible wait.
const PVP_PREPARE_SECONDS = 10;
const resourcePrepareSeconds = (_configuredReadySeconds: number): number =>
  PVP_PREPARE_SECONDS;
''',
    '''// The visible PVP start countdown is ten seconds, but map/song acquisition is
// network-bound and must not share that deadline. Keep a separate server-side
// resource safety budget so the real Geometry Dash download can finish and the
// Ready request can arrive without racing the countdown boundary.
const resourcePrepareSeconds = (configuredReadySeconds: number): number =>
  Math.max(configuredReadySeconds, 60);
''',
)

# Client: the server deadline can now be longer than the visible countdown.
# Always present a maximum ten-second PVP/Deathmatch countdown while keeping
# Placement's own configured countdown behavior untouched.
replace(
    "corum-ranked-mod/src/RankedPopup.cpp",
    '''        auto const countdownRemaining = countdownOverride >= 0.0
            ? countdownOverride
            : authoritativeRemaining
                ? std::max(0.0, static_cast<double>(*authoritativeRemaining) / 1000.0)
                : std::max(0.0, 10.0 - phaseSeconds());
        auto const usedResolvedLevel = level == m_resolvedLevel;
''',
    '''        auto countdownRemaining = countdownOverride >= 0.0
            ? countdownOverride
            : authoritativeRemaining
                ? std::max(0.0, static_cast<double>(*authoritativeRemaining) / 1000.0)
                : std::max(0.0, 10.0 - phaseSeconds());
        if (!RankedRuntime::get().isPlacement()) {
            countdownRemaining = std::min(countdownRemaining, 10.0);
        }
        auto const usedResolvedLevel = level == m_resolvedLevel;
''',
)

# Client: T-5 is the actual PVP/Placement launch threshold. If the exact level
# is ready by then, submit Ready immediately; if it finishes later, submit on
# the first following gate tick. Optional song acquisition must not hold a
# playable map until the hard server resource deadline.
replace(
    "corum-ranked-mod/src/LevelInfoLayer.cpp",
    '''        auto const now = SteadyClock::now();
        auto const hasLevelPayload = mapReady(m_level);
        auto const levelReady = vanillaLevelReadyForPlay();
''',
    '''        auto const now = SteadyClock::now();
        auto const playableStartAt = m_fields->countdownEndsAt - std::chrono::seconds(5);
        auto const hasLevelPayload = mapReady(m_level);
        auto const levelReady = vanillaLevelReadyForPlay();
''',
)
replace(
    "corum-ranked-mod/src/LevelInfoLayer.cpp",
    '''            } else if (now >= m_fields->countdownEndsAt) {
                // The visible Ranked countdown is the resource wait budget for
                // optional music. A broken/slow custom-song API must never keep a
                // fully downloaded playable map stuck after STARTS IN 00.
                // The vanilla widget remains alive, so audio may still finish
                // downloading while gameplay proceeds without it.
''',
    '''            } else if (now >= playableStartAt) {
                // Five seconds remaining is the gameplay launch threshold. A
                // broken/slow optional custom-song API must never keep a fully
                // downloaded playable map from sending Ready at that point.
                // The vanilla widget remains alive, so audio may still finish
                // downloading while gameplay proceeds without it.
''',
)
replace(
    "corum-ranked-mod/src/LevelInfoLayer.cpp",
    '''            now >= (
                runtime.isPlacement()
                    ? m_fields->countdownEndsAt - std::chrono::seconds(5)
                    : m_fields->countdownEndsAt
            ) &&
            readyForStart &&
''',
    '''            now >= playableStartAt &&
            readyForStart &&
''',
)

# Server regression: reaching the end of the visible 10s countdown must not
# cancel a still-preparing Round 1. Ready may arrive immediately afterward.
replace(
    "ranked/apps/server/test/match-flow.integration.test.ts",
    '''    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();

    const winRoundWithThreeClears = async (roundNumber: number) => {
''',
    '''    expect(state.selectedRoundMaps).toBeUndefined();
    expect(state.candidateMaps).toBeNull();
    expect(Date.parse(state.deadlineAt as string) - clock.now().getTime()).toBe(60_000);

    // Regression for beta.10: the visible ten-second PVP countdown must not be
    // the authoritative resource timeout. A map that completes around T=0 still
    // needs time for the client's Ready HTTP request to reach the server.
    clock.advanceSeconds(10);
    state = (await matches.state(
      matchId,
      statusA.matchToken,
      playerA,
    )) as Record<string, any>;
    expect(state.state).toBe("ROUND_PREPARE");

    const winRoundWithThreeClears = async (roundNumber: number) => {
''',
)

# Exact client/server version gate + workspace metadata.
version_files = [
    "corum-ranked-mod/mod.json",
    "ranked/package.json",
    "ranked/apps/server/package.json",
    "ranked/packages/rules/package.json",
    "ranked/package-lock.json",
    "ranked/apps/server/src/session/client-version.ts",
    "ranked/apps/server/test/client-version.test.ts",
    "ranked/apps/server/test/debug-bot.integration.test.ts",
    "ranked/apps/server/test/placement-flow.integration.test.ts",
]
for path in version_files:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    if "0.5.0-beta.10" not in text:
        raise SystemExit(f"{path}: beta.10 version string not found")
    target.write_text(text.replace("0.5.0-beta.10", "0.5.0-beta.11"), encoding="utf-8")

changelog = ROOT / "corum-ranked-mod/changelog.md"
text = changelog.read_text(encoding="utf-8")
header = '''# v0.5.0-beta.11

- Fixed the beta.10 PVP Round 1 map-download timeout race by separating the ten-second visible countdown from the server resource safety deadline.
- PVP and Placement now submit Ready from the five-seconds-remaining launch threshold as soon as the exact GD map is playable; late map completion can still start after the visible countdown instead of being cancelled at T=0.
- Optional song acquisition no longer blocks a playable map past the five-second launch threshold, while the vanilla song widget may continue downloading audio.

'''
if text.startswith("# v0.5.0-beta.11"):
    raise SystemExit("changelog already contains beta.11")
changelog.write_text(header + text, encoding="utf-8")

print("beta11 PVP Ready-race patch applied")
