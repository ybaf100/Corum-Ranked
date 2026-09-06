from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

# ---- RankedRuntime.hpp -----------------------------------------------------
hpp_path = Path("corum-ranked-mod/src/RankedRuntime.hpp")
hpp = hpp_path.read_text()
hpp = replace_once(
    hpp,
    '''struct PlacementView {''',
    '''struct PlacementAttemptView {\n    std::string attemptId;\n    double progressPercent = 0.0;\n    bool cleared = false;\n    double awardedScore = 0.0;\n};\n\nstruct PlacementView {''',
    "PlacementAttemptView",
)
hpp = replace_once(
    hpp,
    '''    std::string activeAttemptId;\n    std::vector<RankedMapView> referenceMaps;''',
    '''    std::string activeAttemptId;\n    std::vector<PlacementAttemptView> currentMapTopAttempts;\n    std::vector<RankedMapView> referenceMaps;''',
    "placement top attempts view",
)
hpp = replace_once(
    hpp,
    '''    bool spectatorActive = false;\n    std::optional<int> opponentProgress;\n    std::optional<int> spectatorCurrentProgress;''',
    '''    bool spectatorActive = false;\n    std::optional<int> opponentProgress;\n    bool opponentFinished = false;\n    bool opponentPractice = false;\n    std::optional<int> spectatorCurrentProgress;''',
    "opponent lifecycle fields",
)
hpp = replace_once(
    hpp,
    '''    [[nodiscard]] bool reportAttemptStart(int levelId);\n    void reportAttemptProgress(int levelId, double progressPercent);''',
    '''    [[nodiscard]] bool reportAttemptStart(int levelId);\n    void reportPracticePresence(bool practice);\n    void reportAttemptProgress(int levelId, double progressPercent);''',
    "practice presence public method",
)
hpp = replace_once(
    hpp,
    '''    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptIntentRequest;\n    geode::async::TaskHolder<geode::utils::web::WebResponse> m_progressRequest;''',
    '''    geode::async::TaskHolder<geode::utils::web::WebResponse> m_attemptIntentRequest;\n    geode::async::TaskHolder<geode::utils::web::WebResponse> m_progressRequest;\n    geode::async::TaskHolder<geode::utils::web::WebResponse> m_presenceRequest;''',
    "presence request holder",
)
hpp_path.write_text(hpp)

# ---- RankedRuntime.cpp -----------------------------------------------------
cpp_path = Path("corum-ranked-mod/src/RankedRuntime.cpp")
cpp = cpp_path.read_text()
cpp = replace_once(
    cpp,
    '''        m_view.placement.currentMapScore = currentMap["mapScore"].asDouble().unwrapOr(0.0);\n        auto const provisional = currentMap["provisionalMapScore"].asDouble().unwrapOr(m_view.placement.currentMapScore);''',
    '''        m_view.placement.currentMapScore = currentMap["mapScore"].asDouble().unwrapOr(0.0);\n        m_view.placement.currentMapTopAttempts.clear();\n        if (auto const topAttempts = currentMap["topAttempts"].asArray(); topAttempts.isOk()) {\n            for (auto const& item : topAttempts.unwrap()) {\n                if (!item.isObject()) continue;\n                PlacementAttemptView attempt;\n                attempt.attemptId = item["attemptId"].asString().unwrapOr("");\n                attempt.progressPercent = item["progressPercent"].asDouble().unwrapOr(0.0);\n                attempt.cleared = item["cleared"].asBool().unwrapOr(false);\n                attempt.awardedScore = item["awardedScore"].asDouble().unwrapOr(0.0);\n                if (!attempt.attemptId.empty()) m_view.placement.currentMapTopAttempts.push_back(std::move(attempt));\n            }\n        }\n        auto const provisional = currentMap["provisionalMapScore"].asDouble().unwrapOr(m_view.placement.currentMapScore);''',
    "parse placement top attempts",
)
cpp = replace_once(
    cpp,
    '''    m_view.match.spectatorActive = false;\n    m_view.match.opponentProgress.reset();\n    m_view.match.spectatorCurrentProgress.reset();''',
    '''    m_view.match.spectatorActive = false;\n    m_view.match.opponentProgress.reset();\n    m_view.match.opponentFinished = root["opponentFinished"].asBool().unwrapOr(false);\n    m_view.match.opponentPractice = root["opponentPractice"].asBool().unwrapOr(false);\n    m_view.match.spectatorCurrentProgress.reset();''',
    "parse opponent lifecycle",
)
cpp = replace_once(
    cpp,
    '''void RankedRuntime::reportAttemptProgress(int levelId, double progressPercent) {''',
    '''void RankedRuntime::reportPracticePresence(bool practice) {\n    if (\n        isPlacement() || m_view.stage != RuntimeStage::Matched ||\n        m_view.match.matchId.empty() || m_matchToken.empty()\n    ) return;\n    matjson::Value body;\n    body["practice"] = practice;\n    auto request = baseRequest(m_sessionToken, m_matchToken);\n    request.bodyJSON(body);\n    m_presenceRequest.spawn(\n        request.post(endpoint("/api/ranked/matches/" + m_view.match.matchId + "/presence")),\n        [this](web::WebResponse response) {\n            if (!successful(response)) {\n                log::warn("Ranked Practice presence sync failed: {}", responseError(response));\n                return;\n            }\n            observeServerNow(response.json().unwrapOr(matjson::Value()));\n        }\n    );\n}\n\nvoid RankedRuntime::reportAttemptProgress(int levelId, double progressPercent) {''',
    "report practice presence",
)
cpp = replace_once(
    cpp,
    '''double RankedRuntime::localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride) const {\n    if (isPlacement()) {''',
    '''double RankedRuntime::localDisplayScore(double progressPercent, std::optional<double> qualifyingPercentOverride) const {\n    if (isPlacement()) {''',
    "locate localDisplayScore",
)
# Insert Death Match branch after the placement early-return block.
cpp = replace_once(
    cpp,
    '''        return static_cast<double>(m_view.placement.provisionalMapScore);\n    }\n    auto const ownIsA = m_view.match.side == "A";''',
    '''        return static_cast<double>(m_view.placement.provisionalMapScore);\n    }\n    auto const ownIsA = m_view.match.side == "A";\n    if (m_view.match.state == "DEATHMATCH_PLAYING") {\n        auto const serverBest = ownIsA ? m_view.match.scoreA : m_view.match.scoreB;\n        if (progressPercent < 0.0) return serverBest;\n        return std::max(serverBest, std::clamp(progressPercent, 0.0, 100.0));\n    }''',
    "deathmatch local raw percent",
)
cpp = replace_once(
    cpp,
    '''    if (qualifying >= 0.0) {\n        optimisticScore = calculateAttemptScore(\n            finalProgress,\n            cleared,\n            qualifying\n        );\n    }''',
    '''    if (m_view.match.state == "DEATHMATCH_PLAYING") {\n        // Death Match is raw best percentage; Qualifying is intentionally absent.\n        optimisticScore = cleared ? 100.0 : finalProgress;\n    } else if (qualifying >= 0.0) {\n        optimisticScore = calculateAttemptScore(\n            finalProgress,\n            cleared,\n            qualifying\n        );\n    }''',
    "deathmatch optimistic raw percent",
)
cpp = replace_once(
    cpp,
    '''    m_progressRequest.cancel();\n    m_progressBusy = false;\n    if (placement && m_view.match.state == "PLACEMENT_RESULT") {''',
    '''    m_progressRequest.cancel();\n    m_progressBusy = false;\n    m_presenceRequest.cancel();\n    if (placement && m_view.match.state == "PLACEMENT_RESULT") {''',
    "cancel presence on dismiss",
)
cpp_path.write_text(cpp)

# ---- PlayLayer.cpp ---------------------------------------------------------
play_path = Path("corum-ranked-mod/src/PlayLayer.cpp")
play = play_path.read_text()
play = replace_once(
    play,
    '''        CCSprite* practiceIndicator = nullptr;\n        std::unordered_map<CCLabelBMFont*, CCLabelBMFont*> hudShadows;''',
    '''        CCSprite* practiceIndicator = nullptr;\n        std::array<CCLabelBMFont*, 2> placementTopLabels {};\n        std::array<std::string, 2> placementTopRecordIds {};\n        std::unordered_map<CCLabelBMFont*, CCLabelBMFont*> hudShadows;''',
    "placement top fields",
)
play = replace_once(
    play,
    '''        m_fields->rankedPractice = m_isPracticeMode;\n        addRankedHud();''',
    '''        m_fields->rankedPractice = m_isPracticeMode;\n        if (m_fields->rankedPractice) runtime.reportPracticePresence(true);\n        addRankedHud();''',
    "initial practice presence",
)
play = replace_once(
    play,
    '''        if (\n            scoringEnabled() &&\n            !m_fields->attemptEndReported && !m_fields->autoExitRequested\n        ) {''',
    '''        if (m_fields->rankedPractice || m_isPracticeMode) {\n            corum::ranked::RankedRuntime::get().reportPracticePresence(false);\n        }\n        if (\n            scoringEnabled() &&\n            !m_fields->attemptEndReported && !m_fields->autoExitRequested\n        ) {''',
    "clear practice presence on quit",
)
play = replace_once(
    play,
    '''            m_fields->rankedPractice = true;\n            PlayLayer::togglePracticeMode(true);''',
    '''            m_fields->rankedPractice = true;\n            runtime.reportPracticePresence(true);\n            PlayLayer::togglePracticeMode(true);''',
    "practice enter presence",
)
play = replace_once(
    play,
    '''        PlayLayer::togglePracticeMode(false);\n        if (transition == corum::ranked::RankedPracticeTransition::LeaveAndExit) {''',
    '''        PlayLayer::togglePracticeMode(false);\n        runtime.reportPracticePresence(false);\n        if (transition == corum::ranked::RankedPracticeTransition::LeaveAndExit) {''',
    "practice leave presence",
)
play = replace_once(
    play,
    '''        if (corum::ranked::RankedRuntime::get().isPlacement()) {\n            setHudVisible(m_fields->qualifyingCaption, false);\n            setHudVisible(m_fields->qualifyingLabel, false);\n            m_fields->placementGrade = corum::ranked::ui::PlacementGradeBadge::create(38.0f);''',
    '''        if (corum::ranked::RankedRuntime::get().isPlacement()) {\n            setHudVisible(m_fields->qualifyingCaption, false);\n            setHudVisible(m_fields->qualifyingLabel, false);\n            for (std::size_t rank = 0; rank < m_fields->placementTopLabels.size(); ++rank) {\n                auto const text = rank == 0 ? "1st - -" : "2nd - -";\n                m_fields->placementTopLabels[rank] = addShadowedHudLabel(\n                    m_fields->hudRoot,\n                    text,\n                    "black",\n                    0.36f,\n                    {1.0f, 0.5f},\n                    {size.width - 10.0f, top - 4.0f - static_cast<float>(rank) * 15.0f},\n                    corum::ranked::ui::Text,\n                    5\n                );\n            }\n            m_fields->placementGrade = corum::ranked::ui::PlacementGradeBadge::create(38.0f);''',
    "create placement top labels",
)

# Insert the Top 2 updater just before updateRankedHud().
play = replace_once(
    play,
    '''    void updateRankedHud() {\n        auto& runtime = corum::ranked::RankedRuntime::get();''',
    '''    void animateHudPosition(CCLabelBMFont* foreground, CCPoint position) {\n        if (!foreground) return;\n        foreground->stopAllActions();\n        foreground->runAction(CCEaseInOut::create(CCMoveTo::create(0.22f, position), 2.0f));\n        if (auto* shadow = hudShadow(foreground)) {\n            shadow->stopAllActions();\n            shadow->runAction(CCEaseInOut::create(\n                CCMoveTo::create(0.22f, {position.x + 0.75f, position.y - 0.75f}),\n                2.0f\n            ));\n        }\n    }\n\n    void updatePlacementTopHud(double liveProgress) {\n        auto& runtime = corum::ranked::RankedRuntime::get();\n        if (!runtime.isPlacement()) return;\n        struct Record {\n            std::string id;\n            double progress = 0.0;\n            double score = 0.0;\n        };\n        std::vector<Record> records;\n        for (auto const& attempt : runtime.view().placement.currentMapTopAttempts) {\n            records.push_back({attempt.attemptId, attempt.progressPercent, attempt.awardedScore});\n        }\n        auto const& activeId = runtime.view().placement.activeAttemptId;\n        if (\n            !activeId.empty() && liveProgress >= 0.0 &&\n            scoringEnabled() && !m_fields->attemptEndReported &&\n            m_fields->qualifyingPercent >= 0.0\n        ) {\n            records.erase(\n                std::remove_if(records.begin(), records.end(), [&](Record const& record) {\n                    return record.id == activeId;\n                }),\n                records.end()\n            );\n            records.push_back({\n                activeId,\n                liveProgress,\n                corum::ranked::calculateAttemptScore(\n                    liveProgress, false, m_fields->qualifyingPercent\n                ),\n            });\n        }\n        std::sort(records.begin(), records.end(), [](Record const& left, Record const& right) {\n            if (std::abs(left.score - right.score) > 0.0005) return left.score > right.score;\n            return left.id < right.id;\n        });\n        if (records.size() > 2) records.resize(2);\n\n        auto const size = CCDirector::sharedDirector()->getWinSize();\n        auto const top = corum::ranked::layoutHud(size.width, size.height).topY;\n        std::array<bool, 2> usedNodes {false, false};\n        for (std::size_t rank = 0; rank < records.size(); ++rank) {\n            int node = -1;\n            for (std::size_t index = 0; index < m_fields->placementTopRecordIds.size(); ++index) {\n                if (m_fields->placementTopRecordIds[index] == records[rank].id) {\n                    node = static_cast<int>(index);\n                    break;\n                }\n            }\n            auto const existingIdentity = node >= 0;\n            if (node < 0) {\n                for (std::size_t index = 0; index < usedNodes.size(); ++index) {\n                    auto const retained = std::any_of(records.begin(), records.end(), [&](Record const& record) {\n                        return m_fields->placementTopRecordIds[index] == record.id;\n                    });\n                    if (!usedNodes[index] && !retained) {\n                        node = static_cast<int>(index);\n                        break;\n                    }\n                }\n            }\n            if (node < 0) node = static_cast<int>(rank);\n            usedNodes[static_cast<std::size_t>(node)] = true;\n            auto* row = m_fields->placementTopLabels[static_cast<std::size_t>(node)];\n            m_fields->placementTopRecordIds[static_cast<std::size_t>(node)] = records[rank].id;\n            auto const ordinal = rank == 0 ? "1st" : "2nd";\n            setHudText(\n                row,\n                fmt::format(\n                    "{} - {}% ({}점)",\n                    ordinal,\n                    formatScore(records[rank].progress),\n                    formatScore(records[rank].score)\n                )\n            );\n            setHudVisible(row, true);\n            auto const target = CCPoint{\n                size.width - 10.0f,\n                top - 4.0f - static_cast<float>(rank) * 15.0f,\n            };\n            if (existingIdentity && std::abs(row->getPositionY() - target.y) > 0.5f) {\n                animateHudPosition(row, target);\n            } else {\n                setHudPosition(row, target);\n            }\n        }\n        for (std::size_t rank = records.size(); rank < 2; ++rank) {\n            int node = -1;\n            for (std::size_t index = 0; index < usedNodes.size(); ++index) {\n                if (!usedNodes[index]) { node = static_cast<int>(index); break; }\n            }\n            if (node < 0) continue;\n            usedNodes[static_cast<std::size_t>(node)] = true;\n            m_fields->placementTopRecordIds[static_cast<std::size_t>(node)].clear();\n            auto* row = m_fields->placementTopLabels[static_cast<std::size_t>(node)];\n            setHudText(row, rank == 0 ? "1st - -" : "2nd - -");\n            setHudVisible(row, true);\n            setHudPosition(row, {\n                size.width - 10.0f,\n                top - 4.0f - static_cast<float>(rank) * 15.0f,\n            });\n        }\n    }\n\n    void updateRankedHud() {\n        auto& runtime = corum::ranked::RankedRuntime::get();''',
    "placement top updater",
)

# Replace the HudInput block details.
play = replace_once(
    play,
    '''            .deathmatchAttemptsUsedA = attemptsUsedA,\n            .deathmatchAttemptsUsedB = attemptsUsedB,\n            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,''',
    '''            .deathmatchAttemptsUsedA = attemptsUsedA,\n            .deathmatchAttemptsUsedB = attemptsUsedB,\n            .deathmatchAttemptsCompletedA = match.deathmatchAttemptsCompletedA,\n            .deathmatchAttemptsCompletedB = match.deathmatchAttemptsCompletedB,\n            .qualifyingPercent = match.currentMap ? match.currentMap->qualifyingPercent : 100.0,''',
    "hud completed attempts",
)
play = replace_once(
    play,
    '''            .opponentProgressEnabled = !placement,\n            .opponentCurrentProgress = match.opponentProgress,\n            .localPractice = m_fields->rankedPractice || m_isPracticeMode,''',
    '''            .opponentProgressEnabled = !placement,\n            .opponentCurrentProgress = match.opponentProgress,\n            .opponentFinished = match.opponentFinished,\n            .opponentPractice = match.opponentPractice,''',
    "hud opponent flags",
)
# Death Match uses the timer budget only; hide the redundant Attempts labels.
play = replace_once(
    play,
    '''        setHudVisible(m_fields->ownAttemptLabel, presentation.deathmatch);\n        setHudVisible(m_fields->opponentAttemptLabel, presentation.deathmatch);\n        if (presentation.deathmatch) {\n            setHudText(m_fields->ownAttemptLabel, presentation.ownAttemptText);\n            setHudText(m_fields->opponentAttemptLabel, presentation.opponentAttemptText);\n        }''',
    '''        setHudVisible(m_fields->ownAttemptLabel, false);\n        setHudVisible(m_fields->opponentAttemptLabel, false);''',
    "hide redundant deathmatch attempt labels",
)
play = replace_once(
    play,
    '''        if (placement) {\n            setHudVisible(m_fields->qualifyingCaption, false);\n            setHudVisible(m_fields->qualifyingLabel, false);\n            if (m_fields->placementGrade) {''',
    '''        if (placement || !presentation.qualifyingVisible) {\n            setHudVisible(m_fields->qualifyingCaption, false);\n            setHudVisible(m_fields->qualifyingLabel, false);\n            if (placement && m_fields->placementGrade) {''',
    "hide deathmatch qualifying",
)
# Feed the same live progress into the Placement Top 2 projection.
play = replace_once(
    play,
    '''        auto const ownLiveScore = runtime.localDisplayScore(\n            liveProgress,\n            m_fields->qualifyingPercent\n        );''',
    '''        auto const ownLiveScore = runtime.localDisplayScore(\n            liveProgress,\n            m_fields->qualifyingPercent\n        );\n        if (placement) updatePlacementTopHud(liveProgress);''',
    "update placement top HUD",
)
play_path.write_text(play)
