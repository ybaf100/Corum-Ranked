from __future__ import annotations

from pathlib import Path
import json
import re
from collections import deque

ROOT = Path(__file__).resolve().parents[1]


def load(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def save(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one match, found {count}")
    return text.replace(old, new, 1)


def sub_once(text: str, pattern: str, replacement: str, label: str) -> str:
    result, count = re.subn(pattern, replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{label}: expected exactly one regex match, found {count}")
    return result


# 1) Keep Placement score precision in the client and expose current-stage evidence.
path = "corum-ranked-mod/src/RankedRuntime.hpp"
text = load(path)
text = replace_once(text, "    std::vector<int> mapScores;", "    std::vector<double> mapScores;", "stage map score precision")
text = replace_once(
    text,
    "    int currentMapScore = 0;\n    int provisionalMapScore = 0;\n    std::string currentMapGrade = \"D\";",
    "    double currentMapScore = 0.0;\n    double provisionalMapScore = 0.0;\n    std::string currentMapGrade = \"D\";\n    std::vector<double> currentStageMapScores;\n    std::vector<std::string> currentStageMapGrades;",
    "placement score fields",
)
text = replace_once(
    text,
    "    void skipPlacement();\n    void continuePlacement();",
    "    void skipPlacement();\n    void continuePlacement();\n    void pausePlacement();",
    "pause placement declaration",
)
save(path, text)


# 2) Parse decimal scores, persist recent stage history, update Grade live, and support pausing after each Stage.
path = "corum-ranked-mod/src/RankedRuntime.cpp"
text = load(path)
text = replace_once(
    text,
    "            if (!root[\"active\"].asBool().unwrapOr(false)) {\n                m_view.placement.active = false;\n                m_view.placement.referenceMaps.clear();",
    "            if (!root[\"active\"].asBool().unwrapOr(false)) {\n                m_view.placement.active = false;\n                m_view.placement.stageResults.clear();\n                if (auto const results = root[\"stageResults\"].asArray(); results.isOk()) {\n                    for (auto const& item : results.unwrap()) {\n                        if (!item.isObject()) continue;\n                        PlacementStageResultView result;\n                        result.stageIndex = static_cast<int>(item[\"stageIndex\"].asInt().unwrapOr(0));\n                        result.pool = static_cast<int>(item[\"pool\"].asInt().unwrapOr(0));\n                        if (auto const scores = item[\"mapScores\"].asArray(); scores.isOk()) {\n                            for (auto const& score : scores.unwrap()) result.mapScores.push_back(score.asDouble().unwrapOr(0.0));\n                        }\n                        if (auto const grades = item[\"mapGrades\"].asArray(); grades.isOk()) {\n                            for (auto const& grade : grades.unwrap()) result.mapGrades.push_back(grade.asString().unwrapOr(\"D\"));\n                        }\n                        result.stageScore = item[\"stageScore\"].asDouble().unwrapOr(0.0);\n                        result.stageGrade = item[\"stageGrade\"].asString().unwrapOr(\"D\");\n                        result.poolDelta = static_cast<int>(item[\"poolDelta\"].asInt().unwrapOr(0));\n                        result.nextPool = static_cast<int>(item[\"nextPool\"].asInt().unwrapOr(1));\n                        m_view.placement.stageResults.push_back(std::move(result));\n                    }\n                }\n                m_view.placement.referenceMaps.clear();",
    "inactive placement history parsing",
)
text = replace_once(
    text,
    "            parsePlacementState(root);\n            setStage(RuntimeStage::Matched, \"Placement recovered. Continue from the saved Stage and Map.\");",
    "            parsePlacementState(root);\n            if (m_view.placement.state == \"STAGE_RESULT\") {\n                setStage(RuntimeStage::Ready, \"Placement Stage saved. Continue whenever you're ready.\");\n            } else {\n                setStage(RuntimeStage::Matched, \"Placement recovered. Continue from the saved Stage and Map.\");\n            }",
    "recover paused stage",
)
text = replace_once(
    text,
    "            parsePlacementState(response.json().unwrapOr(matjson::Value()));\n            m_nextPollAt = std::chrono::steady_clock::now();\n        }\n    );\n}\n\nvoid RankedRuntime::joinQueue()",
    "            parsePlacementState(response.json().unwrapOr(matjson::Value()));\n            setStage(RuntimeStage::Matched, \"Placement resumed. Preparing the next Map.\");\n            m_nextPollAt = std::chrono::steady_clock::now();\n        }\n    );\n}\n\nvoid RankedRuntime::pausePlacement() {\n    if (!isPlacement() || m_view.placement.state != \"STAGE_RESULT\") return;\n    invalidatePollRequest();\n    setStage(RuntimeStage::Ready, \"Placement Stage saved. Continue whenever you're ready.\");\n}\n\nvoid RankedRuntime::joinQueue()",
    "pause placement implementation",
)
text = replace_once(
    text,
    "                    result.mapScores.push_back(static_cast<int>(score.asInt().unwrapOr(0)));",
    "                    result.mapScores.push_back(score.asDouble().unwrapOr(0.0));",
    "stage score parser",
)
text = replace_once(
    text,
    "        m_view.placement.currentMapScore = static_cast<int>(currentMap[\"mapScore\"].asInt().unwrapOr(0));\n        auto const provisional = static_cast<int>(\n            currentMap[\"provisionalMapScore\"].asInt().unwrapOr(m_view.placement.currentMapScore)\n        );",
    "        m_view.placement.currentMapScore = currentMap[\"mapScore\"].asDouble().unwrapOr(0.0);\n        auto const provisional = currentMap[\"provisionalMapScore\"].asDouble().unwrapOr(m_view.placement.currentMapScore);",
    "current map decimal parser",
)
text = replace_once(
    text,
    "        m_view.placement.currentMapGrade = currentMap[\"mapGrade\"].asString().unwrapOr(\"D\");",
    "        m_view.placement.currentMapGrade = currentMap[\"provisionalMapGrade\"].asString().unwrapOr(\n            currentMap[\"mapGrade\"].asString().unwrapOr(\"D\")\n        );",
    "provisional grade parser",
)
text = replace_once(
    text,
    "    auto const currentMap = root[\"currentMap\"];\n    if (auto map = parseMap(currentMap)) {",
    "    m_view.placement.currentStageMapScores.clear();\n    m_view.placement.currentStageMapGrades.clear();\n    if (auto const stageMaps = root[\"currentStageMaps\"].asArray(); stageMaps.isOk()) {\n        for (auto const& stageMap : stageMaps.unwrap()) {\n            m_view.placement.currentStageMapScores.push_back(stageMap[\"mapScore\"].asDouble().unwrapOr(0.0));\n            m_view.placement.currentStageMapGrades.push_back(stageMap[\"mapGrade\"].asString().unwrapOr(\"?\"));\n        }\n    }\n\n    auto const currentMap = root[\"currentMap\"];\n    if (auto map = parseMap(currentMap)) {",
    "current stage map parser",
)
text = replace_once(
    text,
    "        m_view.status = \"Placement Map live. Grade changes only after an Attempt ends.\";",
    "        m_view.status = \"Placement Map live. Score and Grade update from the active Top 2 evidence.\";",
    "live grade status",
)
text = replace_once(
    text,
    "                // This is the only live-game response allowed to mutate the\n                // Placement Grade. Progress telemetry is deliberately ignored.\n                auto const mapScore = root[\"mapScore\"].asInt();\n                if (mapScore.isOk()) {\n                    m_view.placement.currentMapScore = static_cast<int>(mapScore.unwrap());",
    "                auto const mapScore = root[\"mapScore\"].asDouble();\n                if (mapScore.isOk()) {\n                    m_view.placement.currentMapScore = mapScore.unwrap();",
    "attempt end score precision",
)
text = replace_once(
    text,
    "                        auto const provisional = root[\"provisionalMapScore\"].asInt();\n                        if (provisional.isOk()) {\n                            m_view.placement.provisionalMapScore = static_cast<int>(provisional.unwrap());\n                            m_view.match.scoreA = m_view.placement.provisionalMapScore;\n                            ++m_view.revision;\n                        }",
    "                        auto const provisional = root[\"provisionalMapScore\"].asDouble();\n                        if (provisional.isOk()) {\n                            m_view.placement.provisionalMapScore = provisional.unwrap();\n                            m_view.match.scoreA = m_view.placement.provisionalMapScore;\n                        }\n                        auto const provisionalGrade = root[\"provisionalMapGrade\"].asString();\n                        if (provisionalGrade.isOk()) m_view.placement.currentMapGrade = provisionalGrade.unwrap();\n                        if (provisional.isOk() || provisionalGrade.isOk()) ++m_view.revision;",
    "live score and grade client update",
)
save(path, text)


# 3) Server returns live provisional Grade, current-stage map evidence, and latest persisted Stage history.
path = "ranked/apps/server/src/placement/placement.service.ts"
text = load(path)
text = replace_once(
    text,
    "  scoreAttempt,\n  selectPlacementReferenceMaps,",
    "  scoreAttempt,\n  placementGradeForMapScore,\n  selectPlacementReferenceMaps,",
    "placement grade import",
)
text = replace_once(
    text,
    "      const completed = profile !== undefined &&\n        Number(profile.placement_games) >= placementGamesRequired &&\n        profile.displayed_tier !== \"UNRANKED\";\n      return {",
    "      const completed = profile !== undefined &&\n        Number(profile.placement_games) >= placementGamesRequired &&\n        profile.displayed_tier !== \"UNRANKED\";\n      const recentResult = await this.database.query<PlacementRunRow>(\n        `SELECT * FROM ranked_placement_runs\n         WHERE player_id = $1 AND state <> 'CANCELLED'\n         ORDER BY created_at DESC LIMIT 1`,\n        [session.playerId],\n      );\n      const recentRun = recentResult.rows[0];\n      return {",
    "recent placement history query",
)
text = replace_once(
    text,
    "        referenceMaps: completed ? [] : selectPlacementReferenceMaps(snapshot.maps),\n        configGeneration: snapshot.generation,",
    "        referenceMaps: completed ? [] : selectPlacementReferenceMaps(snapshot.maps),\n        stageResults: recentRun ? asArray(recentRun.stage_results) : [],\n        configGeneration: snapshot.generation,",
    "recent placement history payload",
)
text = replace_once(
    text,
    "      return {\n        mapScore: Number(map.map_score),\n        provisionalMapScore: provisional.mapScore,\n        mapGrade: map.map_grade,\n      };\n    });\n    // Progress telemetry is deliberately incapable of changing the HUD Grade.\n    return { ...result, gradeChanged: false, serverNow: now.toISOString() };",
    "      return {\n        mapScore: Number(map.map_score),\n        provisionalMapScore: provisional.mapScore,\n        mapGrade: map.map_grade,\n        provisionalMapGrade: placementGradeForMapScore(provisional.mapScore, policy, Number(map.clear_count)),\n      };\n    });\n    return {\n      ...result,\n      gradeChanged: result.provisionalMapGrade !== result.mapGrade,\n      serverNow: now.toISOString(),\n    };",
    "server live grade response",
)
text = replace_once(
    text,
    "    const currentMap = maps.rows.find((map) =>\n      Number(map.stage_index) === Number(run.current_stage_index) &&\n      Number(map.map_index) === Number(run.current_map_index));",
    "    const currentStageMaps = maps.rows.filter((map) =>\n      Number(map.stage_index) === Number(run.current_stage_index));\n    const currentMap = currentStageMaps.find((map) =>\n      Number(map.map_index) === Number(run.current_map_index));",
    "current stage map selection",
)
text = replace_once(
    text,
    "      stageResults: asArray(run.stage_results),\n      currentMap: currentMap ? {",
    "      stageResults: asArray(run.stage_results),\n      currentStageMaps: currentStageMaps.map((map) => ({\n        mapIndex: Number(map.map_index),\n        mapScore: Number(map.map_score),\n        mapGrade: map.map_grade,\n        state: map.state,\n      })),\n      currentMap: currentMap ? {",
    "current stage map payload",
)
text = replace_once(
    text,
    "        provisionalMapScore: Number(currentMap.map_score),\n        clearTarget: 2,",
    "        provisionalMapScore: Number(currentMap.map_score),\n        provisionalMapGrade: currentMap.map_grade,\n        clearTarget: 2,",
    "state provisional grade",
)
save(path, text)


# 4) Placement result UI: map-end uses the same MAP 1 + MAP 2 > STAGE composition,
#    stages can be saved for later, history shows map/final Grades, and decimals are visible.
path = "corum-ranked-mod/src/RankedPopup.cpp"
text = load(path)
text = replace_once(
    text,
    "                view.historyError,\n                view.history.size()",
    "                view.historyError,\n                view.history.size() + view.placement.stageResults.size()",
    "history presentation key",
)
# Add dedicated Placement progress circles without changing promotion O/X marks.
anchor = "    void addPromotionAura(CCPoint center, CCSize size, bool perfect = false) {"
helper = '''    void addPlacementProgress(\n        int completed,\n        int slots,\n        CCPoint center,\n        float spacing = 17.0f,\n        float scale = 0.30f\n    ) {\n        slots = std::clamp(slots, 1, 9);\n        completed = std::clamp(completed, 0, slots);\n        auto const totalWidth = spacing * static_cast<float>(slots - 1);\n        for (int index = 0; index < slots; ++index) {\n            auto* mark = makeLabel(\n                index < completed ? "●" : "○",\n                scale,\n                {center.x - totalWidth / 2.0f + spacing * static_cast<float>(index), center.y},\n                index < completed ? kGreen : ccc3(145, 151, 166),\n                "black"\n            );\n            m_root->addChild(mark, 6);\n        }\n    }\n\n'''
text = replace_once(text, anchor, helper + anchor, "placement progress helper")
text = replace_once(
    text,
    "            addRatingMarks(profile.placementResults, std::max(1, profile.placementGamesRequired), {center.x, center.y - 47.0f});",
    "            addPlacementProgress(profile.placementGames, std::max(1, profile.placementGamesRequired), {center.x, center.y - 47.0f});",
    "player card placement progress",
)
text = replace_once(
    text,
    "            addRatingMarks(profile.placementResults, std::max(1, profile.placementGamesRequired), {leftCenter.x, leftCenter.y - 51.0f}, 16.0f, 0.30f);",
    "            auto const completedStages = std::max(\n                profile.placementGames,\n                static_cast<int>(view.placement.stageResults.size())\n            );\n            addPlacementProgress(completedStages, std::max(1, profile.placementGamesRequired), {leftCenter.x, leftCenter.y - 51.0f}, 16.0f, 0.30f);",
    "main placement progress",
)
text = replace_once(
    text,
    "                view.placement.required\n                    ? menu_selector(CorumRankedLayer::onOpenPlacement)\n                    : menu_selector(CorumRankedLayer::onJoin)",
    "                view.placement.required\n                    ? (view.placement.active && view.placement.state == \"STAGE_RESULT\"\n                        ? menu_selector(CorumRankedLayer::onContinuePlacement)\n                        : menu_selector(CorumRankedLayer::onOpenPlacement))\n                    : menu_selector(CorumRankedLayer::onJoin)",
    "resume placement play action",
)
text = replace_once(
    text,
    "                view.placement.required\n                    ? tr(\"start-placement\", \"START PLACEMENT\")\n                    : tr(\"start-queue\", \"START QUEUE\"),",
    "                view.placement.required\n                    ? (view.placement.active && view.placement.state == \"STAGE_RESULT\"\n                        ? tr(\"continue-placement\", \"CONTINUE PLACEMENT\")\n                        : tr(\"start-placement\", \"START PLACEMENT\"))\n                    : tr(\"start-queue\", \"START QUEUE\"),",
    "resume placement action text",
)
text = replace_once(
    text,
    "                view.placement.required\n                    ? tr(\"choose-first-pool\", \"CHOOSE YOUR FIRST STAGE POOL\")\n                    : tr(\"find-match\", \"FIND A 1V1 MATCH\"),",
    "                view.placement.required\n                    ? (view.placement.active && view.placement.state == \"STAGE_RESULT\"\n                        ? fmt::format(\"STAGE {} / {} SAVED\", view.placement.currentStageIndex, view.placement.totalStages)\n                        : tr(\"choose-first-pool\", \"CHOOSE YOUR FIRST STAGE POOL\"))\n                    : tr(\"find-match\", \"FIND A 1V1 MATCH\"),",
    "resume placement hint",
)
text = replace_once(
    text,
    "            fmt::format(\"{} / 500\", placement.provisionalMapScore),",
    "            fmt::format(\"{:.1f} / 500\", placement.provisionalMapScore),",
    "live placement decimal score",
)
text = replace_once(
    text,
    "        if (match.state == \"MAP_PLAYING\") stateText = tr(\"placement-map-live\", \"MAP LIVE · GRADE UPDATES AFTER ATTEMPT END\");",
    "        if (match.state == \"MAP_PLAYING\") stateText = tr(\"placement-map-live\", \"MAP LIVE · SCORE + GRADE UPDATE IN REAL TIME\");",
    "live grade copy",
)
# Replace map result with the three-card composition.
map_result = r'''    void renderPlacementMapResult\(RuntimeView const& view\) \{.*?\n    \}\n\n    void renderPlacementStageResult'''
map_replacement = '''    void renderPlacementMapResult(RuntimeView const& view) {\n        auto const size = CCDirector::sharedDirector()->getWinSize();\n        auto const& placement = view.placement;\n        auto const map1Grade = placement.currentStageMapGrades.size() > 0\n            ? placement.currentStageMapGrades[0]\n            : (placement.currentMapIndex == 1 ? placement.currentMapGrade : "?");\n        auto const map2Grade = placement.currentMapIndex >= 2\n            ? (placement.currentStageMapGrades.size() > 1 ? placement.currentStageMapGrades[1] : placement.currentMapGrade)\n            : "?";\n        addScreenHeader(\n            fmt::format("MAP {} {}", placement.currentMapIndex, tr("complete", "COMPLETE")),\n            fmt::format("STAGE {} · {}  {:.1f} / 500", placement.currentStageIndex, tr("server-result", "SERVER RESULT"), placement.currentMapScore),\n            kGold,\n            kCyan\n        );\n\n        auto makeGradeCard = [&](std::string const& caption, std::string const& grade, float x, bool final) {\n            auto* group = CCLayerColor::create({0, 0, 0, 0});\n            group->setContentSize({86.0f, 108.0f});\n            group->setAnchorPoint({0.5f, 0.5f});\n            group->setPosition({x, size.height / 2.0f + 3.0f});\n            group->setCascadeOpacityEnabled(true);\n            auto* panel = makeNeonPanel({86.0f, 108.0f}, {43.0f, 54.0f}, final ? kGold : kBlue, kDeepPanel, 248);\n            group->addChild(panel, 1);\n            auto* title = makeLabel(caption, 0.22f, {43.0f, 93.0f}, final ? kGold : kCyan, "semibold");\n            title->limitLabelWidth(74.0f, 0.22f, 0.15f);\n            group->addChild(title, 3);\n            if (grade == "?") {\n                group->addChild(makeLabel("?", 0.82f, {43.0f, 49.0f}, corum::ranked::ui::TextMuted, "black"), 4);\n            } else {\n                auto* badge = corum::ranked::ui::PlacementGradeBadge::create(final ? 62.0f : 54.0f);\n                if (badge) {\n                    badge->setGrade(grade, false);\n                    badge->setPosition({43.0f, 48.0f});\n                    group->addChild(badge, 4);\n                }\n            }\n            m_root->addChild(group, 5);\n        };\n\n        makeGradeCard("MAP 1", map1Grade, size.width / 2.0f - 150.0f, false);\n        makeGradeCard("MAP 2", map2Grade, size.width / 2.0f - 35.0f, false);\n        makeGradeCard(tr("stage-grade", "STAGE GRADE"), "?", size.width / 2.0f + 140.0f, true);\n        m_root->addChild(makeLabel("+", 0.62f, {size.width / 2.0f - 92.0f, size.height / 2.0f + 3.0f}, kGold, "black"), 6);\n        m_root->addChild(makeLabel(">", 0.55f, {size.width / 2.0f + 52.0f, size.height / 2.0f + 3.0f}, kCyan, "black"), 6);\n\n        addButton(\n            placement.currentMapIndex == 1 ? tr("next-map", "NEXT MAP") : tr("show-stage-result", "SHOW STAGE RESULT"),\n            {size.width / 2.0f, 27.0f},\n            menu_selector(CorumRankedLayer::onContinuePlacement),\n            false,\n            0.60f\n        );\n    }\n\n    void renderPlacementStageResult'''
text = sub_once(text, map_result, map_replacement, "map result three-card UI")
# Stage result becomes a true Stage-end screen with save-for-later.
text = replace_once(
    text,
    "        auto const finalStage = result.stageIndex >= view.placement.totalStages;\n        auto* movement = makeLabel(",
    "        auto const finalStage = result.stageIndex >= view.placement.totalStages;\n        addPlacementProgress(result.stageIndex, view.placement.totalStages, {size.width / 2.0f, 67.0f}, 18.0f, 0.27f);\n        auto* movement = makeLabel(",
    "stage progress circles",
)
text = replace_once(
    text,
    "            {size.width / 2.0f, 54.0f},",
    "            {size.width / 2.0f, 49.0f},",
    "stage movement position",
)
old_button = '''        auto* button = addButton(\n            finalStage\n                ? tr("final-placement", "FINAL PLACEMENT")\n                : tr("next-stage", "NEXT STAGE"),\n            {size.width / 2.0f, 25.0f},\n            menu_selector(CorumRankedLayer::onContinuePlacement),\n            false,\n            0.56f\n        );\n        button->setEnabled(false);\n        button->runAction(CCSequence::create(\n            CCDelayTime::create(0.96f),\n            CallFuncExt::create([button] { button->setEnabled(true); }),\n            nullptr\n        ));'''
new_button = '''        auto* button = addButton(\n            finalStage ? tr("final-placement", "FINAL PLACEMENT") : tr("next-stage", "NEXT STAGE"),\n            {size.width / 2.0f + (finalStage ? 0.0f : 72.0f), 23.0f},\n            menu_selector(CorumRankedLayer::onContinuePlacement),\n            false,\n            0.54f\n        );\n        button->setEnabled(false);\n        button->runAction(CCSequence::create(\n            CCDelayTime::create(0.96f),\n            CallFuncExt::create([button] { button->setEnabled(true); }),\n            nullptr\n        ));\n        if (!finalStage) {\n            auto* later = addButton(\n                tr("play-later", "PLAY LATER"),\n                {size.width / 2.0f - 72.0f, 23.0f},\n                menu_selector(CorumRankedLayer::onPausePlacement),\n                false,\n                0.54f\n            );\n            later->setEnabled(false);\n            later->runAction(CCSequence::create(\n                CCDelayTime::create(0.96f),\n                CallFuncExt::create([later] { later->setEnabled(true); }),\n                nullptr\n            ));\n        }'''
text = replace_once(text, old_button, new_button, "stage save-for-later buttons")
text = replace_once(
    text,
    "    void onContinuePlacement(CCObject*) {\n        m_localMessage.clear();\n        RankedRuntime::get().continuePlacement();\n    }",
    "    void onContinuePlacement(CCObject*) {\n        m_localMessage.clear();\n        RankedRuntime::get().continuePlacement();\n    }\n\n    void onPausePlacement(CCObject*) {\n        m_localMessage.clear();\n        RankedRuntime::get().pausePlacement();\n        m_page = Page::Live;\n    }",
    "pause placement callback",
)
# Replace History list with Placement Stage rows followed by PvP matches.
history_pattern = r'''    void renderHistoryList\(RuntimeView const& view\) \{.*?\n    \}\n\n    void renderHistoryDetail'''
history_replacement = '''    void renderHistoryList(RuntimeView const& view) {\n        auto const size = CCDirector::sharedDirector()->getWinSize();\n        addTopBack(false);\n        addScreenHeader(tr("match-history", "MATCH HISTORY"), tr("recent-matches", "PLACEMENT STAGES + COMPETITIVE MATCHES"), kGold, kCyan);\n        auto const placementCount = std::min<std::size_t>(5, view.placement.stageResults.size());\n        if (view.historyLoading && placementCount == 0) {\n            auto* spinner = LoadingSpinner::create(55.0f);\n            spinner->setPosition({size.width / 2.0f, size.height / 2.0f});\n            m_root->addChild(spinner, 3);\n            return;\n        }\n        if (!view.historyError.empty() && placementCount == 0) {\n            m_root->addChild(makeLabel(view.historyError, 0.27f, {size.width / 2.0f, size.height / 2.0f}, kRed), 3);\n            addButton(tr("retry", "RETRY"), {size.width / 2.0f, size.height / 2.0f - 40.0f}, menu_selector(CorumRankedLayer::onHistory), false, 0.55f);\n            return;\n        }\n        if (view.history.empty() && placementCount == 0) {\n            m_root->addChild(makeLabel(tr("no-completed-matches", "NO COMPLETED MATCHES"), 0.32f, {size.width / 2.0f, size.height / 2.0f}, ccc3(190, 195, 210)), 3);\n            return;\n        }\n\n        std::size_t visualRow = 0;\n        for (std::size_t i = 0; i < placementCount && visualRow < 7; ++i, ++visualRow) {\n            auto const& stage = view.placement.stageResults[i];\n            auto const map1 = stage.mapGrades.size() > 0 ? stage.mapGrades[0] : "?";\n            auto const map2 = stage.mapGrades.size() > 1 ? stage.mapGrades[1] : "?";\n            auto const y = size.height - 67.0f - static_cast<float>(visualRow) * 31.0f;\n            auto* panel = makeNeonPanel({size.width - 110.0f, 23.0f}, {size.width / 2.0f, y}, kGold, kDeepPanel2, 232);\n            m_root->addChild(panel, 1);\n            auto* row = makeLabel(\n                fmt::format("STAGE {}   MAP 1 {}  +  MAP 2 {}  >  {}   {:.1f}/500", stage.stageIndex, map1, map2, stage.stageGrade, stage.stageScore),\n                0.27f,\n                {size.width / 2.0f, y},\n                kGold,\n                "semibold"\n            );\n            row->limitLabelWidth(size.width - 130.0f, 0.27f, 0.17f);\n            m_root->addChild(row, 3);\n        }\n\n        auto const matchSlots = visualRow >= 7 ? 0 : 7 - visualRow;\n        auto const matchCount = std::min<std::size_t>(matchSlots, view.history.size());\n        for (std::size_t i = 0; i < matchCount; ++i, ++visualRow) {\n            auto const& item = view.history[i];\n            auto const y = size.height - 67.0f - static_cast<float>(visualRow) * 31.0f;\n            auto const won = item.side == item.winnerSide;\n            auto* panel = makeNeonPanel({size.width - 110.0f, 23.0f}, {size.width / 2.0f, y}, won ? kGreen : kRed, kDeepPanel2, 232);\n            m_root->addChild(panel, 1);\n            auto const ratingText = item.ownRankedScoreDelta\n                ? fmt::format("{:+d} LP", *item.ownRankedScoreDelta)\n                : tr("placement", "PLACEMENT");\n            auto* row = makeLabel(\n                fmt::format("{}   VS {}   {}", won ? tr("win", "WIN") : tr("loss", "LOSS"), shorten(item.opponentName, 14), ratingText),\n                0.30f,\n                {size.width / 2.0f - 18.0f, y},\n                won ? kGreen : kRed,\n                "semibold"\n            );\n            row->limitLabelWidth(size.width - 190.0f, 0.30f, 0.20f);\n            m_root->addChild(row, 3);\n            auto* detail = addButton(tr("detail", "DETAIL"), {size.width - 72.0f, y}, menu_selector(CorumRankedLayer::onHistoryDetail), false, 0.50f);\n            detail->setTag(static_cast<int>(i));\n        }\n    }\n\n    void renderHistoryDetail'''
text = sub_once(text, history_pattern, history_replacement, "placement history UI")
save(path, text)


# 5) Add circle glyphs to all runtime fonts used by the UI.
path = "corum-ranked-mod/mod.json"
data = json.loads(load(path))
for font in data["resources"]["fonts"].values():
    charset = font.get("charset", "")
    parts = [part for part in charset.split(",") if part]
    for codepoint in ("9675", "9679"):
        if codepoint not in parts:
            parts.append(codepoint)
    font["charset"] = ",".join(parts)
save(path, json.dumps(data, ensure_ascii=False, indent=2) + "\n")


# 6) Remove opaque/near-black backing connected to the borders of Placement Grade PNGs.
#    This preserves dark interior details while making the outer matte genuinely transparent.
try:
    from PIL import Image
except ImportError as exc:
    raise RuntimeError("Pillow is required for Placement Grade alpha repair") from exc

for grade in "sabcd":
    image_path = ROOT / f"corum-ranked-mod/resources/placement-grade-{grade}.png"
    image = Image.open(image_path).convert("RGBA")
    pixels = image.load()
    width, height = image.size
    visited: set[tuple[int, int]] = set()
    queue: deque[tuple[int, int]] = deque()
    for x in range(width):
        queue.append((x, 0)); queue.append((x, height - 1))
    for y in range(height):
        queue.append((0, y)); queue.append((width - 1, y))
    while queue:
        x, y = queue.popleft()
        if (x, y) in visited:
            continue
        visited.add((x, y))
        r, g, b, a = pixels[x, y]
        # Existing transparency is always traversable. Opaque dark pixels are
        # treated as matte only when connected to an image edge.
        matte = a <= 8 or (a > 8 and max(r, g, b) <= 28)
        if not matte:
            continue
        pixels[x, y] = (r, g, b, 0)
        if x > 0: queue.append((x - 1, y))
        if x + 1 < width: queue.append((x + 1, y))
        if y > 0: queue.append((x, y - 1))
        if y + 1 < height: queue.append((x, y + 1))
    image.save(image_path, optimize=True)


# 7) Small regression tests for decimal map scores and live provisional Grade.
path = "ranked/packages/rules/test/placement.test.ts"
text = load(path)
if "preserves half-point Placement scores" not in text:
    marker = "describe(\"Placement scoring\", () => {"
    test = '''\n  it("preserves half-point Placement scores", () => {\n    const result = calculatePlacementMapScore([\n      scorePlacementAttempt("a", 51, false, 50),\n      scorePlacementAttempt("b", 50, false, 50),\n    ], DEFAULT_PLACEMENT_POLICY);\n    expect(result.mapScore).toBe(126.5);\n  });\n'''
    text = replace_once(text, marker, marker + test, "decimal placement regression test")
save(path, text)

print("Placement Stage-session patch applied successfully")
