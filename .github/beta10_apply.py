from pathlib import Path


def read(path):
    return Path(path).read_text(encoding="utf-8")


def write(path, text):
    Path(path).write_text(text, encoding="utf-8")


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected 1 match, got {count}")
    return text.replace(old, new, 1)


def replace_function(text, signature, replacement):
    start = text.find(signature)
    if start < 0:
        raise SystemExit(f"missing function: {signature}")
    brace = text.find("{", start)
    if brace < 0:
        raise SystemExit(f"missing function brace: {signature}")
    depth = 0
    i = brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[:start] + replacement + text[i + 1 :]
        i += 1
    raise SystemExit(f"unclosed function: {signature}")


# PvP: remove the old alpha-era 60-second minimum.
p = "ranked/apps/server/src/match/match.service.ts"
s = read(p)
s = replace_once(
    s,
    """// alpha.10 client flow: 10s start countdown, map download may take 30s,
// and a custom song may be allowed up to 20s. Keep server preparation
// deadlines comfortably beyond the client-side resource window.
const resourcePrepareSeconds = (configuredReadySeconds: number): number =>
  Math.max(configuredReadySeconds, 60);""",
    """// PvP preparation is a real ten-second start countdown. Resource lookup, map
// acquisition, and song acquisition begin immediately inside that window; an
// old alpha-era 60-second safety floor must not extend the visible wait.
const PVP_PREPARE_SECONDS = 10;
const resourcePrepareSeconds = (_configuredReadySeconds: number): number =>
  PVP_PREPARE_SECONDS;""",
    "PvP prepare policy",
)
write(p, s)

# Grade wrapper: preserve the alpha already present in the supplied PNG.
p = "corum-ranked-mod/src/ui/PlacementGradeBadge.hpp"
s = read(p)
s = replace_once(
    s,
    "cocos2d::CCLayerColor* createVisual(std::string const& grade) const;",
    "cocos2d::CCNode* createVisual(std::string const& grade) const;",
    "grade header createVisual",
)
s = replace_once(
    s,
    "cocos2d::CCLayerColor* m_visual = nullptr;",
    "cocos2d::CCNode* m_visual = nullptr;",
    "grade header m_visual",
)
write(p, s)

p = "corum-ranked-mod/src/ui/PlacementGradeBadge.cpp"
s = read(p)
s = replace_once(
    s,
    "CCLayerColor* PlacementGradeBadge::createVisual(std::string const& input) const {",
    "CCNode* PlacementGradeBadge::createVisual(std::string const& input) const {",
    "grade cpp signature",
)
s = replace_once(
    s,
    "    auto* root = CCLayerColor::create({0, 0, 0, 0});\n",
    "    auto* root = CCNode::create();\n",
    "grade cpp wrapper",
)
new_set_grade = r'''void PlacementGradeBadge::setGrade(std::string input, bool animate) {
    auto const next = normalizeGrade(std::move(input));
    auto const transition = m_grade.empty()
        ? PlacementGradeTransition::None
        : placementGradeTransition(m_grade, next);
    if (!m_grade.empty() && transition == PlacementGradeTransition::None) return;

    auto* oldVisual = m_visual;
    auto* nextVisual = createVisual(next);
    if (!nextVisual) return;
    addChild(nextVisual, oldVisual ? oldVisual->getZOrder() + 1 : 1);
    m_visual = nextVisual;
    m_grade = next;

    // Grade PNG alpha is authoritative. Do not fade a transparent black
    // CCLayerColor wrapper to opacity 255, which creates a black rectangle.
    if (oldVisual) oldVisual->removeFromParentAndCleanup(true);
    nextVisual->stopAllActions();
    if (!animate || transition == PlacementGradeTransition::None) {
        nextVisual->setScale(1.0f);
        return;
    }

    if (transition == PlacementGradeTransition::Up) {
        nextVisual->setScale(0.82f);
        nextVisual->runAction(CCSequence::create(
            CCEaseSineOut::create(CCScaleTo::create(0.16f, 1.10f)),
            CCEaseSineInOut::create(CCScaleTo::create(0.12f, 1.0f)),
            nullptr
        ));
    } else {
        nextVisual->setScale(1.04f);
        nextVisual->runAction(CCEaseSineInOut::create(CCScaleTo::create(0.14f, 1.0f)));
    }
}'''
s = replace_function(
    s,
    "void PlacementGradeBadge::setGrade(std::string input, bool animate)",
    new_set_grade,
)
write(p, s)

# Server provisional state: use monotonic persisted progress in both progress and poll responses.
p = "ranked/apps/server/src/placement/placement.service.ts"
s = read(p)
s = replace_once(
    s,
    '''      const updated = await transaction.query(
        `UPDATE ranked_placement_attempts
         SET latest_progress_percent = GREATEST(latest_progress_percent, $1)
         WHERE id = $2 AND map_id = $3 AND ended_at IS NULL AND valid = TRUE`,
        [body.progressPercent, body.attemptId, map.id],
      );
      if (updated.rowCount === 0) throw new NotFoundException("Active Placement attempt not found");''',
    '''      const updated = await transaction.query<{ latest_progress_percent: number }>(
        `UPDATE ranked_placement_attempts
         SET latest_progress_percent = GREATEST(latest_progress_percent, $1)
         WHERE id = $2 AND map_id = $3 AND ended_at IS NULL AND valid = TRUE
         RETURNING latest_progress_percent`,
        [body.progressPercent, body.attemptId, map.id],
      );
      if (updated.rowCount === 0) throw new NotFoundException("Active Placement attempt not found");
      const progressPercent = Number(updated.rows[0]?.latest_progress_percent ?? body.progressPercent);''',
    "progress persistence",
)
s = replace_once(
    s,
    '''      const provisionalAttempt = scoreAttempt(
        body.progressPercent,
        false,
        Number(map.qualifying_percent),
      );''',
    '''      const provisionalAttempt = scoreAttempt(
        progressPercent,
        false,
        Number(map.qualifying_percent),
      );''',
    "progress scoring",
)
s = replace_once(
    s,
    "          progressPercent: body.progressPercent,\n",
    "          progressPercent,\n",
    "progress evidence",
)
s = replace_once(
    s,
    '''    const activeAttempt = currentMap ? await this.database.query<PlacementAttemptRow>(
      `SELECT id, domain_attempt_id, client_start_event_id, client_end_event_id,
              progress_percent, cleared, awarded_score, valid, ended_at
       FROM ranked_placement_attempts
       WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE LIMIT 1`,
      [currentMap.id],
    ) : { rows: [], rowCount: 0 };
    return {''',
    '''    const activeAttempt = currentMap ? await this.database.query<PlacementAttemptRow>(
      `SELECT id, domain_attempt_id, client_start_event_id, client_end_event_id,
              latest_progress_percent AS progress_percent, cleared, awarded_score, valid, ended_at
       FROM ranked_placement_attempts
       WHERE map_id = $1 AND ended_at IS NULL AND valid = TRUE LIMIT 1`,
      [currentMap.id],
    ) : { rows: [], rowCount: 0 };

    let provisionalMapScore = currentMap ? Number(currentMap.map_score) : 0;
    let provisionalMapGrade: PlacementGrade = currentMap?.map_grade ?? "D";
    const active = activeAttempt.rows[0];
    if (currentMap && active) {
      const snapshot = await this.loadSnapshot(this.database, run.config_snapshot_id);
      const policy = this.requirePlacementPolicy(snapshot);
      const attempts = await this.completedAttemptEvidence(this.database, currentMap.id);
      const progressPercent = Math.max(0, Number(active.progress_percent ?? 0));
      const provisionalAttempt = scoreAttempt(
        progressPercent,
        false,
        Number(currentMap.qualifying_percent),
      );
      const provisional = calculatePlacementMapScore([
        ...attempts,
        {
          attemptId: `provisional:${active.id}`,
          progressPercent,
          cleared: false,
          valid: true,
          awardedScore: provisionalAttempt,
        },
      ], policy);
      provisionalMapScore = provisional.mapScore;
      provisionalMapGrade = placementGradeForMapScore(
        provisional.mapScore,
        policy,
        Number(currentMap.clear_count),
      );
    }

    return {''',
    "state provisional calculation",
)
s = replace_once(
    s,
    "        provisionalMapScore: Number(currentMap.map_score),\n        provisionalMapGrade: currentMap.map_grade,",
    "        provisionalMapScore,\n        provisionalMapGrade,",
    "state provisional response",
)
write(p, s)

# Client guard against stale poll responses while a local Attempt remains open.
p = "corum-ranked-mod/src/RankedRuntime.cpp"
s = read(p)
marker = '''std::string joinReasons(std::vector<std::string> const& reasons) {
    std::string result;
    for (auto const& reason : reasons) {
        if (!result.empty()) result += ", ";
        result += reason;
    }
    return result;
}
'''
helper = marker + '''
int placementGradeRank(std::string const& grade) {
    if (grade == "S") return 0;
    if (grade == "A") return 1;
    if (grade == "B") return 2;
    if (grade == "C") return 3;
    return 4;
}
'''
s = replace_once(s, marker, helper, "client grade helper")
s = replace_once(
    s,
    '''        m_view.placement.currentMapGrade = currentMap["provisionalMapGrade"].asString().unwrapOr(
            currentMap["mapGrade"].asString().unwrapOr("D")
        );''',
    '''        auto const incomingGrade = currentMap["provisionalMapGrade"].asString().unwrapOr(
            currentMap["mapGrade"].asString().unwrapOr("D")
        );
        if (
            !hasLocalOpenAttempt() ||
            placementGradeRank(incomingGrade) <= placementGradeRank(m_view.placement.currentMapGrade)
        ) {
            m_view.placement.currentMapGrade = incomingGrade;
        }''',
    "client stale grade guard",
)
write(p, s)

# Placement-specific fixed one-decimal formatting plus compact result cards.
p = "corum-ranked-mod/src/RankedPopup.cpp"
s = read(p)
score_marker = '''std::string formatScore(double value) {
    if (std::abs(value - std::round(value)) < 0.0005) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}
'''
s = replace_once(
    s,
    score_marker,
    score_marker
    + '''
std::string formatPlacementScore(double value) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1) << value;
    return stream.str();
}
''',
    "placement score formatter",
)

map_result = r'''    void renderPlacementMapResult(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const& placement = view.placement;
        auto const map1Known = placement.currentMapIndex >= 1;
        auto const map2Known = placement.currentMapIndex >= 2;
        auto const map1Score = placement.currentStageMapScores.size() > 0
            ? placement.currentStageMapScores[0]
            : (placement.currentMapIndex == 1 ? placement.currentMapScore : 0.0);
        auto const map2Score = placement.currentStageMapScores.size() > 1
            ? placement.currentStageMapScores[1]
            : (placement.currentMapIndex == 2 ? placement.currentMapScore : 0.0);
        auto const map1Grade = placement.currentStageMapGrades.size() > 0
            ? placement.currentStageMapGrades[0]
            : (placement.currentMapIndex == 1 ? placement.currentMapGrade : "?");
        auto const map2Grade = map2Known
            ? (placement.currentStageMapGrades.size() > 1 ? placement.currentStageMapGrades[1] : placement.currentMapGrade)
            : "?";
        auto const pool = view.match.currentMap ? view.match.currentMap->pool : 0;
        addScreenHeader(
            fmt::format("MAP {} {}", placement.currentMapIndex, tr("complete", "COMPLETE")),
            pool > 0
                ? fmt::format("STAGE {} · POOL {}", placement.currentStageIndex, pool)
                : fmt::format("STAGE {}", placement.currentStageIndex),
            kGold,
            kCyan
        );

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, std::string const& scoreText, float x, bool final) {
            auto* group = CCNode::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kBlue, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kCyan, "semibold");
            title->limitLabelWidth(76.0f, 0.20f, 0.14f);
            group->addChild(title, 3);
            if (grade == "?") {
                group->addChild(makeLabel("?", 0.68f, {44.0f, 53.0f}, corum::ranked::ui::TextMuted, "black"), 4);
            } else {
                auto* badge = corum::ranked::ui::PlacementGradeBadge::create(40.0f);
                if (badge) {
                    badge->setGrade(grade, false);
                    badge->setPosition({44.0f, 54.0f});
                    group->addChild(badge, 4);
                }
            }
            auto* score = makeLabel(scoreText, 0.18f, {44.0f, 16.0f}, corum::ranked::ui::TextMuted, "semibold");
            score->limitLabelWidth(76.0f, 0.18f, 0.13f);
            group->addChild(score, 4);
            m_root->addChild(group, 5);
        };

        auto const center = size.width / 2.0f;
        makeGradeCard("MAP 1", map1Grade, map1Known ? fmt::format("{} / 500", formatPlacementScore(map1Score)) : "--", center - 112.0f, false);
        makeGradeCard("MAP 2", map2Grade, map2Known ? fmt::format("{} / 500", formatPlacementScore(map2Score)) : "--", center, false);
        makeGradeCard(tr("stage-grade", "STAGE GRADE"), "?", "--", center + 112.0f, true);
        m_root->addChild(makeLabel("+", 0.48f, {center - 56.0f, size.height / 2.0f + 4.0f}, kGold, "black"), 6);
        m_root->addChild(makeLabel(">", 0.46f, {center + 56.0f, size.height / 2.0f + 4.0f}, kCyan, "black"), 6);

        addButton(
            placement.currentMapIndex == 1 ? tr("next-map", "NEXT MAP") : tr("show-stage-result", "SHOW STAGE RESULT"),
            {center, 27.0f},
            menu_selector(CorumRankedLayer::onContinuePlacement),
            false,
            0.60f
        );
    }'''
s = replace_function(s, "    void renderPlacementMapResult(RuntimeView const& view)", map_result)

stage_result = r'''    void renderPlacementStageResult(RuntimeView const& view) {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        if (view.placement.stageResults.empty()) return;
        auto const result = view.placement.stageResults.back();
        auto const map1Grade = result.mapGrades.size() > 0 ? result.mapGrades[0] : "D";
        auto const map2Grade = result.mapGrades.size() > 1 ? result.mapGrades[1] : "D";
        auto const map1Score = result.mapScores.size() > 0 ? result.mapScores[0] : 0.0;
        auto const map2Score = result.mapScores.size() > 1 ? result.mapScores[1] : 0.0;
        auto const audioKey = fmt::format("{}:{}", view.placement.runId, result.stageIndex);
        addScreenHeader(
            fmt::format("STAGE {} {}", result.stageIndex, tr("result", "RESULT")),
            fmt::format("POOL {}", result.pool),
            kGold,
            kCyan
        );

        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, double scoreValue, float x, bool final) {
            auto* group = CCNode::create();
            group->setContentSize({88.0f, 102.0f});
            group->setAnchorPoint({0.5f, 0.5f});
            group->setPosition({x, size.height / 2.0f + 4.0f});
            auto* panel = makeNeonPanel({88.0f, 102.0f}, {44.0f, 51.0f}, final ? kGold : kBlue, kDeepPanel, 248);
            group->addChild(panel, 1);
            auto* title = makeLabel(caption, 0.20f, {44.0f, 87.0f}, final ? kGold : kCyan, "semibold");
            title->limitLabelWidth(76.0f, 0.20f, 0.14f);
            group->addChild(title, 3);
            auto* badge = corum::ranked::ui::PlacementGradeBadge::create(40.0f);
            if (badge) {
                badge->setGrade(grade, false);
                badge->setPosition({44.0f, 54.0f});
                group->addChild(badge, 4);
            }
            auto* score = makeLabel(
                fmt::format("{} / 500", formatPlacementScore(scoreValue)),
                0.18f,
                {44.0f, 16.0f},
                final ? kGold : corum::ranked::ui::TextMuted,
                "semibold"
            );
            score->limitLabelWidth(76.0f, 0.18f, 0.13f);
            group->addChild(score, 4);
            m_root->addChild(group, 5);
            return group;
        };

        auto const center = size.width / 2.0f;
        auto* left = makeGradeCard("MAP 1", map1Grade, map1Score, center - 112.0f, false);
        auto* right = makeGradeCard("MAP 2", map2Grade, map2Score, center, false);
        auto* finalGroup = makeGradeCard(tr("stage-grade", "STAGE GRADE"), result.stageGrade, result.stageScore, center + 112.0f, true);
        m_root->addChild(makeLabel("+", 0.48f, {center - 56.0f, size.height / 2.0f + 4.0f}, kGold, "black"), 6);
        m_root->addChild(makeLabel(">", 0.46f, {center + 56.0f, size.height / 2.0f + 4.0f}, kCyan, "black"), 6);

        left->runAction(CCSequence::create(
            CCDelayTime::create(0.16f),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {2.0f, 0.0f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {-2.0f, 0.0f})),
            nullptr
        ));
        right->runAction(CCSequence::create(
            CCDelayTime::create(0.16f),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {-2.0f, 0.0f})),
            CCEaseSineInOut::create(CCMoveBy::create(0.16f, {2.0f, 0.0f})),
            nullptr
        ));
        finalGroup->setScale(0.84f);
        finalGroup->runAction(CCSequence::create(
            CCDelayTime::create(0.44f),
            CCEaseSineOut::create(CCScaleTo::create(0.14f, 1.08f)),
            CCEaseSineInOut::create(CCScaleTo::create(0.12f, 1.0f)),
            nullptr
        ));
        if (m_stageResultAudioPlayedKey != audioKey) {
            finalGroup->runAction(CCSequence::create(
                CCDelayTime::create(0.44f),
                CallFuncExt::create([this, audioKey, grade = result.stageGrade] {
                    m_stageResultAudioPlayedKey = audioKey;
                    RankedAudioManager::get().setMode(
                        grade == "S" || grade == "A" || grade == "B"
                            ? RankedAudioMode::ResultWin
                            : RankedAudioMode::ResultLose
                    );
                }),
                nullptr
            ));
        }

        auto const finalStage = result.stageIndex >= view.placement.totalStages;
        addPlacementProgress(result.stageIndex, view.placement.totalStages, {center, 67.0f}, 18.0f, 0.27f);
        auto* movement = makeLabel(
            finalStage
                ? tr("all-placement-maps-complete", "ALL 10 MAPS COMPLETE")
                : fmt::format("{}  POOL {}  ({:+d})", tr("next", "NEXT"), result.nextPool, result.poolDelta),
            0.30f,
            {center, 49.0f},
            result.poolDelta > 0 ? kGreen : result.poolDelta < 0 ? kRed : corum::ranked::ui::TextMuted,
            "black"
        );
        m_root->addChild(movement, 5);
        auto* button = addButton(
            finalStage ? tr("final-placement", "FINAL PLACEMENT") : tr("next-stage", "NEXT STAGE"),
            {center + (finalStage ? 0.0f : 72.0f), 23.0f},
            menu_selector(CorumRankedLayer::onContinuePlacement),
            false,
            0.54f
        );
        button->setEnabled(false);
        button->runAction(CCSequence::create(
            CCDelayTime::create(0.76f),
            CallFuncExt::create([button] { button->setEnabled(true); }),
            nullptr
        ));
        if (!finalStage) {
            auto* later = addButton(
                tr("play-later", "PLAY LATER"),
                {center - 72.0f, 23.0f},
                menu_selector(CorumRankedLayer::onPausePlacement),
                false,
                0.54f
            );
            later->setEnabled(false);
            later->runAction(CCSequence::create(
                CCDelayTime::create(0.76f),
                CallFuncExt::create([later] { later->setEnabled(true); }),
                nullptr
            ));
        }
    }'''
s = replace_function(s, "    void renderPlacementStageResult(RuntimeView const& view)", stage_result)
s = s.replace("formatScore(result.stageScore)", "formatPlacementScore(result.stageScore)")
write(p, s)

# Release/version metadata.
for path in [
    "corum-ranked-mod/mod.json",
    "ranked/package.json",
    "ranked/apps/server/package.json",
    "ranked/packages/rules/package.json",
    "ranked/package-lock.json",
    "ranked/apps/server/src/session/client-version.ts",
    "ranked/apps/server/test/client-version.test.ts",
    "ranked/apps/server/test/debug-bot.integration.test.ts",
    "ranked/apps/server/test/placement-flow.integration.test.ts",
]:
    text = read(path)
    text = text.replace("v0.5.0-beta.9", "v0.5.0-beta.10")
    text = text.replace("0.5.0-beta.9", "0.5.0-beta.10")
    write(path, text)

p = "corum-ranked-mod/changelog.md"
s = read(p)
entry = """# v0.5.0-beta.10

- Restored PvP Round and Death Match preparation to an exact ten-second countdown instead of the legacy 60-second resource floor.
- Fixed Placement Grade rendering so transparent PNGs stay transparent, reduced Grade art sizing, and rebuilt Map/Stage results as a compact `MAP 1 + MAP 2 > STAGE GRADE` three-card layout with per-card one-decimal scores.
- Unified live Placement provisional score/Grade evidence between progress updates and normal state polling, preserving monotonic Attempt progress and preventing D/C/B Grade oscillation from stale poll responses.
- Kept Stage-by-Stage Placement sessions, Play Later/resume, progress dots, and Placement history while standardizing Placement score presentation to one decimal.
- Updated the exact client/server version gate and workspace metadata to `v0.5.0-beta.10`.

"""
if not s.startswith("# v0.5.0-beta.10"):
    s = entry + s
write(p, s)

# Fast source-level invariants before full CI/builds.
assert "const PVP_PREPARE_SECONDS = 10;" in read("ranked/apps/server/src/match/match.service.ts")
assert "Math.max(configuredReadySeconds, 60)" not in read("ranked/apps/server/src/match/match.service.ts")
assert "auto* root = CCNode::create();" in read("corum-ranked-mod/src/ui/PlacementGradeBadge.cpp")
assert "CCLayerColor* PlacementGradeBadge::createVisual" not in read("corum-ranked-mod/src/ui/PlacementGradeBadge.cpp")
assert "RETURNING latest_progress_percent" in read("ranked/apps/server/src/placement/placement.service.ts")
assert "latest_progress_percent AS progress_percent" in read("ranked/apps/server/src/placement/placement.service.ts")
