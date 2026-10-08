#include "BatchSubmitPopup.hpp"

#include "ApiClient.hpp"
#include "EvidenceUploader.hpp"
#include "Scoring.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace geode::prelude;

namespace {

struct ServerRecord {
    int percent = 0;
    double score = 0.0;
};

struct BatchCandidate {
    corum::MapInfo map;
    int sourceLevelID = 0;
    int best = 0;
    int attempts = 0;
    int jumps = 0;
    int serverPercent = 0;
    double previousScore = 0.0;
    double estimatedScore = 0.0;
    std::string evidenceID;
    bool syncOnly = false;
    bool submitted = false;
    bool succeeded = false;
    std::string resultMessage;
};

int integerValue(matjson::Value const& value) {
    auto const numeric = value.asInt();
    if (numeric.isOk()) return static_cast<int>(numeric.unwrap());
    return numFromString<int>(
        value.asString().unwrapOr("")
    ).unwrapOr(0);
}

double numberValue(matjson::Value const& value) {
    auto const numeric = value.asDouble();
    if (numeric.isOk()) return numeric.unwrap();
    return numFromString<double>(
        value.asString().unwrapOr("")
    ).unwrapOr(0.0);
}

std::string shorten(std::string value, std::size_t maximumLength) {
    if (value.size() <= maximumLength) return value;
    if (maximumLength <= 3) return value.substr(0, maximumLength);
    value.resize(maximumLength - 3);
    value += "...";
    return value;
}

std::string responseError(matjson::Value const& root) {
    if (!root.contains("error") || !root["error"].isObject()) {
        return "Unknown API error.";
    }

    auto const code = root["error"]["code"].asString().unwrapOr("");
    if (code == "UNKNOWN_ACTION") return "The server does not support this action.";
    if (code == "INTERNAL_ERROR") return "The server could not process the request.";
    if (code == "EMPTY_BODY") return "The request body was empty.";
    if (code == "BODY_TOO_LARGE") return "The request body was too large.";
    if (code == "EMPTY_BATCH") return "There were no records to submit.";
    if (code == "BATCH_TOO_LARGE") return "Too many records were selected.";
    if (code == "INVALID_RECORD") return "One of the records was invalid.";
    if (code == "INVALID_JSON") return "The request was not valid JSON.";
    if (code == "MAP_NOT_FOUND") return "This level is no longer listed on Corum.";
    if (code == "BELOW_MINIMUM") return "The record is below the current minimum.";
    if (code == "PLAYER_DISABLED") return "Submission is disabled for this account.";
    if (code == "CLIENT_OUTDATED") return "C Integration must be updated before submitting.";
    if (code == "CLIENT_VERSION_REQUIRED") return "The server could not verify this mod version.";
    if (code == "CLIENT_PLATFORM_UNSUPPORTED") return "Submission is unavailable on this platform.";
    if (code == "INVALID_LEVEL_ID") return "The level ID is invalid.";
    if (code == "INVALID_TOKEN") return "The server rejected the record credentials.";
    if (code == "UNAUTHORIZED") return "This Geometry Dash account is not authorized.";
    return code.empty() ? "Unknown API error." : fmt::format("API error: {}", code);
}

std::string freshURL(std::string url) {
    auto const now = std::chrono::system_clock::now().time_since_epoch();
    auto const nonce = std::chrono::duration_cast<std::chrono::milliseconds>(
        now
    ).count();
    return fmt::format(
        "{}{}corumNonce={}",
        url,
        url.find('?') == std::string::npos ? "?" : "&",
        nonce
    );
}

class CorumBatchSubmitPopup final : public Popup {
protected:
    enum class ViewState {
        Loading,
        Review,
        Submitting,
        Results,
        Error,
    };

    async::TaskHolder<web::WebResponse> m_catalogRequest;
    async::TaskHolder<web::WebResponse> m_scoresRequest;
    async::TaskHolder<web::WebResponse> m_submitRequest;
    CCNode* m_contentLayer = nullptr;
    CCMenuItemSpriteExtra* m_actionButton = nullptr;
    ViewState m_state = ViewState::Loading;
    int m_accountID = 0;
    std::string m_username;
    bool m_lookupStarted = false;
    bool m_catalogFinished = false;
    bool m_scoresFinished = false;
    std::string m_lookupError;
    std::vector<corum::MapInfo> m_maps;
    std::unordered_map<int, ServerRecord> m_serverRecords;
    std::vector<BatchCandidate> m_candidates;
    double m_currentTotal = 0.0;
    double m_expectedTotal = 0.0;
    int m_successCount = 0;
    int m_failureCount = 0;

    bool init() override {
        if (!Popup::init(440.0f, 300.0f)) return false;

        auto account = GJAccountManager::get();
        if (account) {
            m_accountID = account->m_accountID;
            m_username = std::string(account->m_username);
        }

        m_contentLayer = CCNode::create();
        m_contentLayer->setContentSize(m_size);
        m_contentLayer->setAnchorPoint({0.0f, 0.0f});
        m_contentLayer->setPosition(CCPointZero);
        m_mainLayer->addChild(m_contentLayer, 2);

        showLoading("Preparing Corum...");

        if (m_accountID <= 0 || m_username.empty()) {
            showError("Please sign in to your Geometry Dash account.");
            return true;
        }

        corum::ApiClient::initializeSession();
        if (corum::ApiClient::startupReady()) {
            if (!corum::ApiClient::submissionAllowed()) {
                showError(
                    "C Integration must be updated before records can be submitted."
                );
                return true;
            }
            beginLookup();
        } else if (
            corum::ApiClient::startupStatus() == corum::StartupStatus::Failed
        ) {
            showError(
                "C Integration did not initialize. Restart the game and check "
                "the startup error."
            );
        } else {
            schedule(
                schedule_selector(CorumBatchSubmitPopup::checkStartup),
                0.1f
            );
        }
        return true;
    }

    void checkStartup(float) {
        if (!corum::ApiClient::startupFinished()) return;
        unschedule(schedule_selector(CorumBatchSubmitPopup::checkStartup));

        if (!corum::ApiClient::startupReady()) {
            showError(
                "C Integration did not initialize. Restart the game and check "
                "the startup error."
            );
            return;
        }
        if (!corum::ApiClient::submissionAllowed()) {
            showError(
                "C Integration must be updated before records can be submitted."
            );
            return;
        }
        beginLookup();
    }

    void beginLookup() {
        if (m_lookupStarted) return;
        m_lookupStarted = true;
        showLoading("Scanning maps and records...");

        auto catalogRequest = web::WebRequest();
        catalogRequest.header("User-Agent", corum::ApiClient::userAgent());
        catalogRequest.timeout(std::chrono::seconds(30));
        m_catalogRequest.spawn(
            catalogRequest.get(freshURL(corum::ApiClient::catalogURL())),
            [this](web::WebResponse response) {
                auto const catalog =
                    corum::ApiClient::parseCatalogResponse(response);
                if (!catalog.ok) {
                    m_lookupError = catalog.message.empty()
                        ? "Could not load the Corum map list."
                        : catalog.message;
                } else {
                    m_maps = catalog.maps;
                }
                m_catalogFinished = true;
                finishLookupIfReady();
            }
        );

        auto scoresRequest = web::WebRequest();
        scoresRequest.header("User-Agent", corum::ApiClient::userAgent());
        scoresRequest.timeout(std::chrono::seconds(30));
        m_scoresRequest.spawn(
            scoresRequest.get(
                freshURL(corum::ApiClient::playerRecordsURL(m_accountID))
            ),
            [this](web::WebResponse response) {
                auto const error = parseScoresResponse(response);
                if (!error.empty() && m_lookupError.empty()) {
                    m_lookupError = error;
                }
                m_scoresFinished = true;
                finishLookupIfReady();
            }
        );
    }

    std::string parseScoresResponse(web::WebResponse& response) {
        if (response.code() < 200 || response.code() >= 300) {
            auto const networkError = std::string(response.errorMessage());
            return networkError.empty()
                ? fmt::format("score list HTTP {}", response.code())
                : networkError;
        }

        auto const root = response.json().unwrapOr(matjson::Value());
        if (
            !root.isObject() ||
            !root["ok"].asBool().unwrapOr(false) ||
            !root.contains("players") ||
            !root["players"].isArray()
        ) {
            return "The server returned an invalid score list.";
        }

        auto const playersResult = root["players"].asArray();
        if (playersResult.isErr()) {
            return "The server returned an invalid player list.";
        }

        for (auto const& player : playersResult.unwrap()) {
            if (!player.isObject()) continue;
            if (integerValue(player["accountId"]) != m_accountID) continue;

            m_currentTotal = std::max(0.0, numberValue(player["score"]));
            if (!player.contains("records") || !player["records"].isArray()) {
                return "";
            }

            auto const recordsResult = player["records"].asArray();
            if (recordsResult.isErr()) return "";

            for (auto const& record : recordsResult.unwrap()) {
                if (!record.isObject()) continue;
                auto const levelID = integerValue(record["levelId"]);
                auto const percent = std::clamp(
                    integerValue(record["percent"]),
                    0,
                    100
                );
                if (levelID <= 0 || percent <= 0) continue;

                auto const score = std::max(
                    0.0,
                    numberValue(record["score"])
                );
                auto& saved = m_serverRecords[levelID];
                if (percent >= saved.percent) {
                    saved.percent = percent;
                    saved.score = score;
                }
            }
            break;
        }
        return "";
    }

    void finishLookupIfReady() {
        if (!m_catalogFinished || !m_scoresFinished) return;
        if (!m_lookupError.empty()) {
            showError(m_lookupError);
            return;
        }

        scanLocalRecords();
        showReview();
    }

    void scanLocalRecords() {
        m_candidates.clear();
        m_expectedTotal = m_currentTotal;

        auto levelManager = GameLevelManager::sharedState();
        if (!levelManager) return;

        for (auto const& map : m_maps) {
            auto level = levelManager->getSavedLevel(map.levelID);
            auto sourceLevelID = map.levelID;
            if (map.alternateLevelID > 0) {
                auto alternateLevel =
                    levelManager->getSavedLevel(map.alternateLevelID);
                auto const primaryBest = level
                    ? std::clamp(level->getNormalPercent(), 0, 100)
                    : 0;
                auto const alternateBest = alternateLevel
                    ? std::clamp(
                        alternateLevel->getNormalPercent(),
                        0,
                        100
                    )
                    : 0;

                if (alternateLevel && (!level || alternateBest > primaryBest)) {
                    level = alternateLevel;
                    sourceLevelID = map.alternateLevelID;
                }
            }
            if (!level) continue;

            auto const best = std::clamp(level->getNormalPercent(), 0, 100);
            if (static_cast<double>(best) < map.minimumRecord) continue;

            auto const existing = m_serverRecords.find(map.levelID);
            auto const serverPercent = existing == m_serverRecords.end()
                ? 0
                : existing->second.percent;
            auto const previousScore = existing == m_serverRecords.end()
                ? 0.0
                : existing->second.score;
            auto const syncOnly =
                best >= 100 &&
                best == serverPercent &&
                corum::hasPendingEvidenceForSubmission(
                    map.levelID,
                    m_accountID
                );
            if (best < serverPercent) continue;
            if (best == serverPercent && !syncOnly) continue;

            auto const estimatedScore = syncOnly
                ? previousScore
                : corum::recordScore(
                    map.rank,
                    static_cast<double>(best),
                    map.minimumRecord
                );
            m_expectedTotal += estimatedScore - previousScore;
            m_candidates.push_back({
                .map = map,
                .sourceLevelID = sourceLevelID,
                .best = best,
                .attempts = std::max(0, static_cast<int>(level->m_attempts)),
                .jumps = std::max(0, static_cast<int>(level->m_jumps)),
                .serverPercent = serverPercent,
                .previousScore = previousScore,
                .estimatedScore = estimatedScore,
                .syncOnly = syncOnly,
            });
        }

        std::sort(
            m_candidates.begin(),
            m_candidates.end(),
            [](BatchCandidate const& left, BatchCandidate const& right) {
                if (left.map.rank != right.map.rank) {
                    return left.map.rank < right.map.rank;
                }
                return left.map.title < right.map.title;
            }
        );
        m_expectedTotal = std::max(0.0, m_expectedTotal);
    }

    void clearView() {
        if (m_actionButton) {
            m_actionButton->removeFromParentAndCleanup(true);
            m_actionButton = nullptr;
        }
        m_contentLayer->removeAllChildrenWithCleanup(true);
    }

    void showLoading(char const* message) {
        m_state = ViewState::Loading;
        clearView();
        setTitle("Submit Corum Records", "goldFont.fnt", 0.68f, 22.0f);
        setClosable(false);

        auto spinner = LoadingSpinner::create(72.0f);
        spinner->setPosition({220.0f, 168.0f});
        m_contentLayer->addChild(spinner, 3);

        auto label = CCLabelBMFont::create(message, "bigFont.fnt");
        label->setPosition({220.0f, 105.0f});
        label->limitLabelWidth(340.0f, 0.52f, 0.28f);
        m_contentLayer->addChild(label, 3);
    }

    void showReview() {
        m_state = ViewState::Review;
        clearView();
        setTitle("Submit Corum Records", "goldFont.fnt", 0.68f, 22.0f);
        setClosable(true);

        if (m_candidates.empty()) {
            auto icon = CCSprite::createWithSpriteFrameName(
                "GJ_completesIcon_001.png"
            );
            limitNodeSize(icon, {62.0f, 62.0f}, 1.7f, 0.1f);
            icon->setPosition({220.0f, 184.0f});
            m_contentLayer->addChild(icon, 3);

            auto headline = CCLabelBMFont::create(
                "No new records to submit",
                "bigFont.fnt"
            );
            headline->setColor(ccc3(135, 255, 155));
            headline->setPosition({220.0f, 135.0f});
            headline->limitLabelWidth(340.0f, 0.52f, 0.3f);
            m_contentLayer->addChild(headline, 3);

            auto details = SimpleTextArea::create(
                "Only local bests that meet the minimum and beat the server "
                "record are included.",
                "bigFont.fnt",
                0.32f,
                330.0f
            );
            details->setAlignment(kCCTextAlignmentCenter);
            details->setColor(ccc4(255, 255, 255, 255));
            details->setMaxLines(3);
            details->setLinePadding(-2.0f);
            details->setPosition({220.0f, 94.0f});
            m_contentLayer->addChild(details, 3);

            addActionButton(
                "Close",
                menu_selector(CorumBatchSubmitPopup::onClose),
                "GJ_button_01.png",
                true
            );
            return;
        }

        auto countLabel = CCLabelBMFont::create(
            fmt::format(
                "{} RECORD{} READY",
                m_candidates.size(),
                m_candidates.size() == 1 ? "" : "S"
            ).c_str(),
            "goldFont.fnt"
        );
        countLabel->setScale(0.40f);
        countLabel->setPosition({220.0f, 244.0f});
        m_contentLayer->addChild(countLabel, 3);

        auto pointsLabel = CCLabelBMFont::create(
            fmt::format("Expected total: {:.2f} PTS", m_expectedTotal).c_str(),
            "bigFont.fnt"
        );
        pointsLabel->setColor(ccc3(90, 235, 255));
        pointsLabel->setPosition({220.0f, 225.0f});
        pointsLabel->limitLabelWidth(350.0f, 0.42f, 0.25f);
        m_contentLayer->addChild(pointsLabel, 3);

        addListBackground();
        auto list = ScrollLayer::create({384.0f, 142.0f});
        list->setID("batch-review-list"_spr);
        list->setPosition({28.0f, 67.0f});
        list->m_contentLayer->setLayout(
            ScrollLayer::createDefaultListLayout(3.0f)
        );
        for (auto const& candidate : m_candidates) {
            list->m_contentLayer->addChild(createCandidateRow(candidate));
        }
        list->m_contentLayer->updateLayout();
        list->scrollToTop();
        m_contentLayer->addChild(list, 3);

        addActionButton(
            m_candidates.size() == 1 ? "Submit" : "Submit All",
            menu_selector(CorumBatchSubmitPopup::onSubmitAll),
            "GJ_button_01.png",
            true
        );
    }

    void addListBackground() {
        auto background = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        background->setContentSize({394.0f, 150.0f});
        background->setPosition({220.0f, 138.0f});
        background->setColor(ccc3(42, 24, 18));
        background->setOpacity(195);
        m_contentLayer->addChild(background, 1);
    }

    CCNode* createCandidateRow(BatchCandidate const& candidate) {
        auto row = CCNode::create();
        row->setContentSize({378.0f, 38.0f});

        auto background = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        background->setContentSize({378.0f, 36.0f});
        background->setPosition({189.0f, 19.0f});
        background->setColor(
            candidate.serverPercent > 0
                ? ccc3(45, 79, 91)
                : ccc3(45, 63, 40)
        );
        background->setOpacity(175);
        row->addChild(background, 0);

        auto rankLabel = CCLabelBMFont::create(
            candidate.map.rank > 0
                ? fmt::format("#{}", candidate.map.rank).c_str()
                : "#?",
            "goldFont.fnt"
        );
        rankLabel->setAnchorPoint({0.0f, 0.5f});
        rankLabel->setPosition({8.0f, 25.5f});
        rankLabel->setScale(0.28f);
        row->addChild(rankLabel, 2);

        auto titleLabel = CCLabelBMFont::create(
            candidate.map.title.c_str(),
            "bigFont.fnt"
        );
        titleLabel->setAnchorPoint({0.0f, 0.5f});
        titleLabel->setPosition({44.0f, 25.5f});
        titleLabel->limitLabelWidth(205.0f, 0.34f, 0.24f);
        row->addChild(titleLabel, 2);

        auto const progressText = candidate.syncOnly
            ? fmt::format(
                "SYNC {}%  |  MAX {:.2f}",
                candidate.best,
                corum::baseScore(candidate.map.rank)
            )
            : candidate.serverPercent > 0
            ? fmt::format(
                "UPDATE {}% -> {}%  |  MAX {:.2f}",
                candidate.serverPercent,
                candidate.best,
                corum::baseScore(candidate.map.rank)
            )
            : fmt::format(
                "NEW {}%  |  MAX {:.2f}",
                candidate.best,
                corum::baseScore(candidate.map.rank)
            );
        auto progressLabel = CCLabelBMFont::create(
            progressText.c_str(),
            "goldFont.fnt"
        );
        progressLabel->setAnchorPoint({0.0f, 0.5f});
        progressLabel->setPosition({44.0f, 9.5f});
        progressLabel->setScale(0.23f);
        progressLabel->setColor(ccc3(210, 220, 235));
        row->addChild(progressLabel, 2);

        auto scoreLabel = CCLabelBMFont::create(
            fmt::format("{:.2f} PTS", candidate.estimatedScore).c_str(),
            "bigFont.fnt"
        );
        scoreLabel->setAnchorPoint({1.0f, 0.5f});
        scoreLabel->setColor(ccc3(90, 235, 255));
        scoreLabel->setPosition({369.0f, 19.0f});
        scoreLabel->limitLabelWidth(105.0f, 0.32f, 0.22f);
        row->addChild(scoreLabel, 2);

        return row;
    }

    void addActionButton(
        char const* text,
        SEL_MenuHandler selector,
        char const* background,
        bool enabled
    ) {
        auto sprite = ButtonSprite::create(
            text,
            "bigFont.fnt",
            background,
            0.82f
        );
        sprite->setScale(0.76f);

        m_actionButton = CCMenuItemSpriteExtra::create(
            sprite,
            this,
            selector
        );
        m_actionButton->setID("batch-popup-action-button"_spr);
        m_actionButton->setPosition({220.0f, 37.0f});
        m_actionButton->setEnabled(enabled);
        m_buttonMenu->addChild(m_actionButton);
    }

    void onSubmitAll(CCObject*) {
        if (!corum::ApiClient::submissionAllowed()) {
            corum::ApiClient::showUpdateRequiredWarning();
            return;
        }
        if (m_state != ViewState::Review || m_candidates.empty()) return;
        m_successCount = 0;
        m_failureCount = 0;
        showSubmitting();
        prepareBatchEvidence(0);
    }

    void showSubmitting() {
        m_state = ViewState::Submitting;
        clearView();
        setTitle("Submitting Records", "goldFont.fnt", 0.72f, 22.0f);
        setClosable(false);

        auto spinner = LoadingSpinner::create(76.0f);
        spinner->setPosition({220.0f, 171.0f});
        m_contentLayer->addChild(spinner, 3);

        auto progressLabel = CCLabelBMFont::create(
            fmt::format(
                "Submitting {} record{}...",
                m_candidates.size(),
                m_candidates.size() == 1 ? "" : "s"
            ).c_str(),
            "bigFont.fnt"
        );
        progressLabel->setColor(ccc3(90, 235, 255));
        progressLabel->setPosition({220.0f, 111.0f});
        progressLabel->limitLabelWidth(340.0f, 0.50f, 0.28f);
        m_contentLayer->addChild(progressLabel, 3);

        auto batchLabel = CCLabelBMFont::create(
            "Preparing and sending one batch",
            "goldFont.fnt"
        );
        batchLabel->setPosition({220.0f, 86.0f});
        batchLabel->setColor(ccc3(220, 225, 235));
        batchLabel->limitLabelWidth(340.0f, 0.34f, 0.22f);
        m_contentLayer->addChild(batchLabel, 3);
    }

    void prepareBatchEvidence(std::size_t index) {
        if (index >= m_candidates.size()) {
            submitBatch();
            return;
        }

        auto& candidate = m_candidates[index];
        candidate.evidenceID.clear();
        if (candidate.best < 100) {
            prepareBatchEvidence(index + 1);
            return;
        }

        corum::prepareEvidenceForSubmission(
            candidate.map.levelID,
            candidate.sourceLevelID,
            m_accountID,
            [this, index](corum::EvidencePreparationResult result) {
                if (!result.success) {
                    finishBatchWithError(
                        result.error.empty()
                            ? "Could not prepare all records for upload."
                            : result.error
                    );
                    return;
                }
                m_candidates[index].evidenceID =
                    std::move(result.evidenceID);
                prepareBatchEvidence(index + 1);
            }
        );
    }

    void submitBatch() {
        if (!corum::ApiClient::submissionAllowed()) {
            finishBatchWithError(
                "C Integration must be updated before records can be submitted."
            );
            return;
        }
        auto const now = std::chrono::system_clock::now().time_since_epoch();
        matjson::Value body;
        body["action"] = "batchRecords";
        body["gdAccountId"] = m_accountID;
        body["gdUsername"] = m_username;
        body["platform"] = GEODE_PLATFORM_NAME;
        body["modVersion"] = Mod::get()->getVersion().toVString();
        body["gameVersion"] = GEODE_GD_VERSION_STRING;
        body["geodeVersion"] = Loader::get()->getVersion().toVString();
        body["loadedMods"] = matjson::Value::array();
        for (auto const& loadedMod : corum::ApiClient::loadedMods()) {
            body["loadedMods"].push(loadedMod);
        }
        body["clientTimestamp"] = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()
        );
        body["records"] = matjson::Value::array();

        for (auto const& candidate : m_candidates) {
            matjson::Value record;
            record["levelId"] = candidate.sourceLevelID;
            record["percent"] = candidate.best;
            record["attempts"] = candidate.attempts;
            record["jumps"] = candidate.jumps;
            record["playTimeMs"] = 0;
            if (candidate.best >= 100 && !candidate.evidenceID.empty()) {
                record["evidenceId"] = candidate.evidenceID;
            }
            body["records"].push(std::move(record));
        }

        auto request = web::WebRequest();
        request.header("User-Agent", corum::ApiClient::userAgent());
        request.bodyJSON(body);
        request.followRedirects(false);
        request.timeout(std::chrono::seconds(90));
        m_submitRequest.spawn(
            request.post(corum::ApiClient::baseURL()),
            [this](web::WebResponse response) {
                handleInitialSubmitResponse(std::move(response));
            }
        );
    }

    void handleInitialSubmitResponse(web::WebResponse response) {
        if (!response.redirected()) {
            handleSubmitResponse(std::move(response));
            return;
        }

        auto location = response.header("location");
        if (!location) location = response.header("Location");
        if (!location || location->empty()) {
            finishBatchWithError("The server returned an unreadable redirect.");
            return;
        }

        auto request = web::WebRequest();
        request.header("User-Agent", corum::ApiClient::userAgent());
        request.followRedirects(true);
        request.timeout(std::chrono::seconds(90));
        m_submitRequest.spawn(
            request.get(std::string(*location)),
            [this](web::WebResponse redirectedResponse) {
                handleSubmitResponse(std::move(redirectedResponse));
            }
        );
    }

    void handleSubmitResponse(web::WebResponse response) {
        if (response.code() < 200 || response.code() >= 300) {
            auto message = fmt::format("HTTP {}", response.code());
            if (!response.errorMessage().empty()) {
                message = fmt::format(
                    "{}: {}",
                    message,
                    shorten(std::string(response.errorMessage()), 90)
                );
            }
            finishBatchWithError(message);
            return;
        }

        auto const root = response.json().unwrapOr(matjson::Value());
        if (!root.isObject()) {
            finishBatchWithError("Could not parse the server response.");
            return;
        }
        if (!root["ok"].asBool().unwrapOr(false)) {
            finishBatchWithError(responseError(root));
            return;
        }

        if (!root.contains("results") || !root["results"].isArray()) {
            finishBatchWithError("The server did not return batch results.");
            return;
        }

        for (auto& candidate : m_candidates) {
            candidate.submitted = true;
            candidate.succeeded = false;
            candidate.resultMessage = "The server did not return a result.";
        }

        auto const results = root["results"].asArray();
        if (results.isErr()) {
            finishBatchWithError("The server returned invalid batch results.");
            return;
        }

        for (auto const& result : results.unwrap()) {
            if (!result.isObject()) continue;
            auto const levelID = integerValue(result["levelId"]);
            auto const found = std::find_if(
                m_candidates.begin(),
                m_candidates.end(),
                [levelID](BatchCandidate const& candidate) {
                    return candidate.map.levelID == levelID;
                }
            );
            if (found == m_candidates.end()) continue;

            found->submitted = true;
            found->succeeded = result["ok"].asBool().unwrapOr(false);
            if (found->succeeded) {
                auto const unchanged =
                    result["unchanged"].asBool().unwrapOr(false);
                found->resultMessage = unchanged
                    ? "Server kept the existing best."
                    : "Record submitted.";
                if (found->best >= 100 && !found->evidenceID.empty()) {
                    corum::markEvidenceSubmissionComplete(
                        found->map.levelID,
                        m_accountID
                    );
                }
                corum::ApiClient::clearCachedMap(levelID, m_accountID);
            } else {
                found->resultMessage = responseError(result);
            }
        }

        m_successCount = 0;
        m_failureCount = 0;
        for (auto const& candidate : m_candidates) {
            if (candidate.succeeded) ++m_successCount;
            else ++m_failureCount;
        }
        showResults();
    }

    void finishBatchWithError(std::string const& message) {
        m_successCount = 0;
        m_failureCount = static_cast<int>(m_candidates.size());
        for (auto& candidate : m_candidates) {
            candidate.submitted = true;
            candidate.succeeded = false;
            candidate.resultMessage = message;
        }
        showResults();
    }

    double resultingTotal() const {
        auto total = m_currentTotal;
        for (auto const& candidate : m_candidates) {
            if (candidate.succeeded) {
                total += candidate.estimatedScore - candidate.previousScore;
            }
        }
        return std::max(0.0, total);
    }

    void showResults() {
        m_state = ViewState::Results;
        clearView();
        setTitle(
            m_failureCount == 0 ? "Submission Complete" : "Batch Finished",
            "goldFont.fnt",
            0.72f,
            22.0f
        );
        setClosable(true);

        auto summaryLabel = CCLabelBMFont::create(
            fmt::format(
                "{} SUBMITTED  |  {} FAILED",
                m_successCount,
                m_failureCount
            ).c_str(),
            "goldFont.fnt"
        );
        summaryLabel->setScale(0.40f);
        summaryLabel->setColor(
            m_failureCount == 0
                ? ccc3(135, 255, 155)
                : ccc3(255, 180, 100)
        );
        summaryLabel->setPosition({220.0f, 244.0f});
        m_contentLayer->addChild(summaryLabel, 3);

        auto pointsLabel = CCLabelBMFont::create(
            fmt::format("Resulting total: {:.2f} PTS", resultingTotal()).c_str(),
            "bigFont.fnt"
        );
        pointsLabel->setColor(ccc3(90, 235, 255));
        pointsLabel->setPosition({220.0f, 225.0f});
        pointsLabel->limitLabelWidth(350.0f, 0.42f, 0.25f);
        m_contentLayer->addChild(pointsLabel, 3);

        addListBackground();
        auto list = ScrollLayer::create({384.0f, 142.0f});
        list->setID("batch-results-list"_spr);
        list->setPosition({28.0f, 67.0f});
        list->m_contentLayer->setLayout(
            ScrollLayer::createDefaultListLayout(3.0f)
        );
        for (auto const& candidate : m_candidates) {
            list->m_contentLayer->addChild(createResultRow(candidate));
        }
        list->m_contentLayer->updateLayout();
        list->scrollToTop();
        m_contentLayer->addChild(list, 3);

        addActionButton(
            "Close",
            menu_selector(CorumBatchSubmitPopup::onClose),
            "GJ_button_01.png",
            true
        );

        auto const message = fmt::format(
            "{} Corum record{} submitted, {} failed.",
            m_successCount,
            m_successCount == 1 ? "" : "s",
            m_failureCount
        );
        notify(
            message,
            m_failureCount == 0
                ? NotificationIcon::Success
                : NotificationIcon::Error
        );
    }

    CCNode* createResultRow(BatchCandidate const& candidate) {
        auto row = CCNode::create();
        row->setContentSize({378.0f, 38.0f});

        auto background = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        background->setContentSize({378.0f, 36.0f});
        background->setPosition({189.0f, 19.0f});
        background->setColor(
            candidate.succeeded
                ? ccc3(40, 83, 46)
                : ccc3(104, 42, 38)
        );
        background->setOpacity(180);
        row->addChild(background, 0);

        auto titleLabel = CCLabelBMFont::create(
            candidate.map.title.c_str(),
            "bigFont.fnt"
        );
        titleLabel->setAnchorPoint({0.0f, 0.5f});
        titleLabel->setPosition({10.0f, 25.5f});
        titleLabel->limitLabelWidth(235.0f, 0.34f, 0.24f);
        row->addChild(titleLabel, 2);

        auto resultLabel = CCLabelBMFont::create(
            shorten(candidate.resultMessage, 58).c_str(),
            "goldFont.fnt"
        );
        resultLabel->setAnchorPoint({0.0f, 0.5f});
        resultLabel->setPosition({10.0f, 9.5f});
        resultLabel->setScale(0.23f);
        resultLabel->setColor(
            candidate.succeeded
                ? ccc3(155, 255, 170)
                : ccc3(255, 155, 145)
        );
        row->addChild(resultLabel, 2);

        auto scoreLabel = CCLabelBMFont::create(
            fmt::format("{:.2f}", candidate.estimatedScore).c_str(),
            "bigFont.fnt"
        );
        scoreLabel->setAnchorPoint({1.0f, 0.5f});
        scoreLabel->setPosition({340.0f, 19.0f});
        scoreLabel->setColor(ccc3(90, 235, 255));
        scoreLabel->limitLabelWidth(80.0f, 0.30f, 0.21f);
        row->addChild(scoreLabel, 2);

        auto icon = CCSprite::createWithSpriteFrameName(
            candidate.succeeded
                ? "GJ_completesIcon_001.png"
                : "GJ_deleteIcon_001.png"
        );
        limitNodeSize(icon, {25.0f, 25.0f}, 1.2f, 0.1f);
        icon->setPosition({361.0f, 19.0f});
        row->addChild(icon, 3);
        return row;
    }

    void showError(std::string const& message) {
        m_state = ViewState::Error;
        clearView();
        setTitle("Batch Scan Failed", "goldFont.fnt", 0.72f, 22.0f);
        setClosable(true);

        auto icon = CCSprite::createWithSpriteFrameName(
            "GJ_deleteIcon_001.png"
        );
        limitNodeSize(icon, {66.0f, 66.0f}, 1.7f, 0.1f);
        icon->setPosition({220.0f, 183.0f});
        m_contentLayer->addChild(icon, 3);

        auto headline = CCLabelBMFont::create(
            "Could not scan records",
            "bigFont.fnt"
        );
        headline->setColor(ccc3(255, 115, 115));
        headline->setPosition({220.0f, 137.0f});
        headline->setScale(0.50f);
        m_contentLayer->addChild(headline, 3);

        auto details = SimpleTextArea::create(
            shorten(message, 220),
            "bigFont.fnt",
            0.32f,
            340.0f
        );
        details->setAlignment(kCCTextAlignmentCenter);
        details->setColor(ccc4(255, 255, 255, 255));
        details->setMaxLines(4);
        details->setLinePadding(-2.0f);
        details->setPosition({220.0f, 94.0f});
        m_contentLayer->addChild(details, 3);

        addActionButton(
            "Close",
            menu_selector(CorumBatchSubmitPopup::onClose),
            "GJ_button_01.png",
            true
        );
        notify(message, NotificationIcon::Error);
    }

    void setClosable(bool closable) {
        m_closeBtn->setVisible(closable);
        m_closeBtn->setEnabled(closable);
    }

    void notify(std::string const& message, NotificationIcon icon) {
        if (!Mod::get()->getSettingValue<bool>("submit-notifications")) return;
        Notification::create(shorten(message, 150), icon, 4.0f)->show();
    }

    void onClose(CCObject* sender) override {
        if (
            m_state == ViewState::Loading ||
            m_state == ViewState::Submitting
        ) {
            return;
        }
        Popup::onClose(sender);
    }

public:
    static CorumBatchSubmitPopup* create() {
        auto popup = new CorumBatchSubmitPopup();
        if (popup && popup->init()) {
            popup->autorelease();
            return popup;
        }
        delete popup;
        return nullptr;
    }
};

} // namespace

namespace corum {

void showBatchSubmitPopup() {
    if (ApiClient::isOutdated()) {
        ApiClient::showUpdateRequiredWarning();
        return;
    }
    if (auto popup = CorumBatchSubmitPopup::create()) {
        popup->show();
    }
}

} // namespace corum
