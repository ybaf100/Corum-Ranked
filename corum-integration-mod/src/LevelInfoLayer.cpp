#include "ApiClient.hpp"
#include "EvidenceUploader.hpp"
#include "Scoring.hpp"

#include <Geode/Geode.hpp>
#include <Geode/loader/Loader.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/ui/BasedButtonSprite.hpp>
#include <Geode/ui/LoadingSpinner.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextArea.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <utility>

using namespace geode::prelude;

namespace {

std::string formatPercent(double value) {
    auto const rounded = std::round(value);
    if (std::abs(value - rounded) < 0.001) {
        return fmt::format("{}", static_cast<int>(rounded));
    }
    return fmt::format("{:.1f}", value);
}

std::string responseError(matjson::Value const& root) {
    if (!root.contains("error") || !root["error"].isObject()) return "Unknown API error.";

    auto const code = root["error"]["code"].asString().unwrapOr("");
    if (code == "UNKNOWN_ACTION") return "The server does not support this action.";
    if (code == "INTERNAL_ERROR") return "The server could not process the request.";
    if (code == "EMPTY_BODY") return "The request body was empty.";
    if (code == "BODY_TOO_LARGE") return "The request body was too large.";
    if (code == "INVALID_JSON") return "The request was not valid JSON.";
    if (code == "MAP_NOT_FOUND") return "This level is not listed on Corum.";
    if (code == "BELOW_MINIMUM") return "Your best record is below this level's minimum.";
    if (code == "PLAYER_DISABLED") return "Record submission is disabled for this account.";
    if (code == "CLIENT_OUTDATED") return "C Integration must be updated before submitting.";
    if (code == "CLIENT_VERSION_REQUIRED") return "The server could not verify this mod version.";
    if (code == "CLIENT_PLATFORM_UNSUPPORTED") return "Record submission is unavailable on this platform.";
    if (code == "INVALID_LEVEL_ID") return "The level ID is invalid.";
    if (code == "INVALID_TOKEN") return "The server rejected the record credentials.";
    if (code == "UNAUTHORIZED") return "This Geometry Dash account is not authorized.";
    return code.empty() ? "Unknown API error." : fmt::format("API error: {}", code);
}

ccColor4F color4(ccColor3B color, float opacity = 1.0f) {
    return ccc4f(
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        opacity
    );
}

ccColor3B readableTextColor(ccColor3B background) {
    auto const luminance = (
        0.299 * static_cast<double>(background.r) +
        0.587 * static_cast<double>(background.g) +
        0.114 * static_cast<double>(background.b)
    ) / 255.0;

    return luminance > 0.58
        ? ccc3(10, 10, 10)
        : ccc3(255, 255, 255);
}

CCDrawNode* createPaperPlaneIcon() {
    auto icon = CCDrawNode::create();
    icon->setContentSize({30.0f, 26.0f});
    icon->setAnchorPoint({0.5f, 0.5f});

    auto const white = ccc4f(1.0f, 1.0f, 1.0f, 1.0f);
    auto const shade = ccc4f(0.72f, 0.94f, 1.0f, 1.0f);
    auto const outline = ccc4f(0.05f, 0.05f, 0.05f, 1.0f);

    CCPoint upperWing[] {
        {1.5f, 22.5f},
        {28.5f, 13.5f},
        {10.0f, 12.0f},
    };
    CCPoint lowerWing[] {
        {10.0f, 12.0f},
        {28.5f, 13.5f},
        {5.0f, 2.5f},
    };

    icon->drawPolygon(upperWing, 3, white, 1.4f, outline);
    icon->drawPolygon(lowerWing, 3, shade, 1.4f, outline);
    return icon;
}

class CorumSubmitPopup final : public Popup {
protected:
    enum class ViewState {
        Loading,
        Form,
        Submitting,
        Success,
        Error,
    };

    async::TaskHolder<web::WebResponse> m_request;
    CCNode* m_contentLayer = nullptr;
    CCMenuItemSpriteExtra* m_actionButton = nullptr;
    ViewState m_state = ViewState::Form;
    int m_levelID = 0;
    int m_canonicalLevelID = 0;
    int m_sourceLevelID = 0;
    int m_best = 0;
    int m_attempts = 0;
    int m_jumps = 0;
    double m_minimum = 100.0;
    double m_estimatedScore = 0.0;
    double m_maximumScore = 0.0;
    int m_accountID = 0;
    bool m_eligible = false;
    bool m_scoreLocked = false;
    bool m_scoreWillUpdate = false;
    std::string m_submissionEvidenceID;

    bool init(GJGameLevel* level, int best) {
        if (!level || !Popup::init(320.0f, 310.0f)) return false;

        m_levelID = static_cast<int>(level->m_levelID);
        m_sourceLevelID = m_levelID;
        m_best = best;
        m_attempts = std::max(0, static_cast<int>(level->m_attempts));
        m_jumps = std::max(0, static_cast<int>(level->m_jumps));

        if (auto account = GJAccountManager::get()) {
            m_accountID = account->m_accountID;
        }

        m_contentLayer = CCNode::create();
        m_contentLayer->setContentSize(m_size);
        m_contentLayer->setAnchorPoint({0.0f, 0.0f});
        m_contentLayer->setPosition(CCPointZero);
        m_mainLayer->addChild(m_contentLayer, 2);

        showLoading();
        refreshMapData();
        return true;
    }

    void selectBestLocalRecord(corum::MapInfo const& map) {
        auto levelManager = GameLevelManager::sharedState();
        if (!levelManager) return;

        auto selectIfHigher = [this](GJGameLevel* level, int sourceLevelID) {
            if (!level || sourceLevelID <= 0) return;

            auto const best = std::clamp(level->getNormalPercent(), 0, 100);
            if (best <= m_best) return;

            m_sourceLevelID = sourceLevelID;
            m_best = best;
            m_attempts = std::max(0, static_cast<int>(level->m_attempts));
            m_jumps = std::max(0, static_cast<int>(level->m_jumps));
        };

        selectIfHigher(
            levelManager->getSavedLevel(map.levelID),
            map.levelID
        );
        if (map.alternateLevelID > 0 && map.alternateLevelID != map.levelID) {
            selectIfHigher(
                levelManager->getSavedLevel(map.alternateLevelID),
                map.alternateLevelID
            );
        }
    }

    void configureMap(corum::MapInfo const& map) {
        m_canonicalLevelID = map.levelID;
        selectBestLocalRecord(map);
        m_minimum = map.minimumRecord;
        m_eligible = static_cast<double>(m_best) >= m_minimum;
        m_scoreWillUpdate =
            map.hasFrozenScore &&
            m_best > map.serverPercent;
        m_scoreLocked = map.hasFrozenScore && !m_scoreWillUpdate;
        m_estimatedScore = m_scoreLocked
            ? map.frozenScore
            : corum::recordScore(
                map.rank,
                static_cast<double>(m_best),
                m_minimum
            );
        m_maximumScore = corum::baseScore(map.rank);
    }

    void refreshMapData() {
        auto request = web::WebRequest();
        request.header("User-Agent", corum::ApiClient::userAgent());
        request.timeout(std::chrono::seconds(20));

        m_request.spawn(
            request.get(corum::ApiClient::mapURL(m_levelID, m_accountID)),
            [this](web::WebResponse response) {
                auto result = corum::ApiClient::parseMapResponse(response);
                if (result.status == corum::LookupStatus::Listed && result.map) {
                    configureMap(*result.map);
                    showForm();
                    return;
                }

                if (result.status == corum::LookupStatus::NotListed) {
                    showLoadFailure("This level is no longer listed on Corum.");
                    return;
                }

                auto message = result.message;
                if (message.empty()) {
                    message = result.errorCode.empty()
                        ? "Could not load the latest Corum data."
                        : fmt::format("API error: {}", result.errorCode);
                }
                showLoadFailure(message);
            }
        );
    }

    void clearView() {
        if (m_actionButton) {
            m_actionButton->removeFromParentAndCleanup(true);
            m_actionButton = nullptr;
        }
        m_contentLayer->removeAllChildrenWithCleanup(true);
    }

    void showLoading() {
        m_state = ViewState::Loading;
        clearView();
        setTitle("Submit Record", "goldFont.fnt", 0.78f, 24.0f);
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);

        auto spinner = LoadingSpinner::create(66.0f);
        spinner->setPosition({160.0f, 174.0f});
        m_contentLayer->addChild(spinner, 3);

        auto label = CCLabelBMFont::create(
            "Loading latest data...",
            "bigFont.fnt"
        );
        label->setPosition({160.0f, 112.0f});
        label->limitLabelWidth(245.0f, 0.52f, 0.3f);
        m_contentLayer->addChild(label, 3);
    }

    void showLoadFailure(std::string const& message) {
        showResult(false, "Loading Failed", message);
        notify(message, NotificationIcon::Error);
    }

    void showForm() {
        m_state = ViewState::Form;
        clearView();
        setTitle("Submit Record", "goldFont.fnt", 0.78f, 24.0f);
        m_closeBtn->setVisible(true);
        m_closeBtn->setEnabled(true);

        addRecordField(
            "Required",
            fmt::format("{}%", formatPercent(m_minimum)),
            232.0f,
            ccc3(255, 220, 95)
        );

        auto recordColor = ccc3(255, 245, 120);
        if (!m_eligible) {
            recordColor = ccc3(255, 90, 90);
        } else if (static_cast<double>(m_best) > m_minimum) {
            recordColor = ccc3(105, 255, 125);
        }
        addRecordField(
            "Your Record",
            fmt::format("{}%", m_best),
            170.0f,
            recordColor
        );

        addPointsField(108.0f);

        auto status = CCLabelBMFont::create(
            !m_eligible
                ? "Minimum record not reached"
                : m_scoreLocked
                    ? "Submit a higher best to update points"
                    : m_scoreWillUpdate
                        ? "Higher best will update points"
                        : "Ready to submit",
            "bigFont.fnt"
        );
        status->setScale(0.38f);
        status->setColor(
            m_eligible ? ccc3(150, 255, 160) : ccc3(255, 145, 145)
        );
        status->setPosition({160.0f, 70.0f});
        m_contentLayer->addChild(status, 3);

        auto submitSprite = ButtonSprite::create(
            "Submit",
            "bigFont.fnt",
            m_eligible ? "GJ_button_01.png" : "GJ_button_05.png",
            0.82f
        );
        submitSprite->setScale(0.82f);

        m_actionButton = CCMenuItemSpriteExtra::create(
            submitSprite,
            this,
            menu_selector(CorumSubmitPopup::onSubmit)
        );
        m_actionButton->setID("popup-submit-button"_spr);
        m_actionButton->setPosition({160.0f, 37.0f});
        m_actionButton->setEnabled(m_eligible);
        m_buttonMenu->addChild(m_actionButton);
    }

    void addRecordField(
        char const* caption,
        std::string const& value,
        float centerY,
        ccColor3B valueColor
    ) {
        auto captionLabel = CCLabelBMFont::create(caption, "goldFont.fnt");
        captionLabel->setScale(0.42f);
        captionLabel->setAnchorPoint({0.0f, 0.5f});
        captionLabel->setPosition({48.0f, centerY + 24.0f});
        m_contentLayer->addChild(captionLabel, 3);

        auto field = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        field->setContentSize({230.0f, 42.0f});
        field->setPosition({160.0f, centerY});
        field->setColor(ccc3(45, 26, 18));
        field->setOpacity(185);
        m_contentLayer->addChild(field, 1);

        auto valueLabel = CCLabelBMFont::create(value.c_str(), "bigFont.fnt");
        valueLabel->setScale(0.56f);
        valueLabel->setColor(valueColor);
        valueLabel->setPosition({160.0f, centerY + 1.0f});
        m_contentLayer->addChild(valueLabel, 3);
    }

    void addPointsField(float centerY) {
        auto captionLabel = CCLabelBMFont::create(
            m_scoreLocked
                ? "Locked Points"
                : m_scoreWillUpdate
                    ? "Updated Points"
                    : "Estimated Points",
            "goldFont.fnt"
        );
        captionLabel->setScale(0.42f);
        captionLabel->setAnchorPoint({0.0f, 0.5f});
        captionLabel->setPosition({48.0f, centerY + 24.0f});
        m_contentLayer->addChild(captionLabel, 3);

        auto field = CCScale9Sprite::create(
            "square02_001.png",
            {0.0f, 0.0f, 80.0f, 80.0f}
        );
        field->setContentSize({230.0f, 42.0f});
        field->setPosition({160.0f, centerY});
        field->setColor(ccc3(45, 26, 18));
        field->setOpacity(185);
        m_contentLayer->addChild(field, 1);

        auto divider = CCDrawNode::create();
        divider->drawSegment(
            {160.0f, centerY - 15.0f},
            {160.0f, centerY + 15.0f},
            0.7f,
            ccc4f(1.0f, 1.0f, 1.0f, 0.28f)
        );
        m_contentLayer->addChild(divider, 2);

        auto currentCaption = CCLabelBMFont::create(
            m_scoreLocked ? "LOCKED" : m_scoreWillUpdate ? "NEW" : "CURRENT",
            "goldFont.fnt"
        );
        currentCaption->setScale(0.25f);
        currentCaption->setPosition({102.5f, centerY + 10.0f});
        m_contentLayer->addChild(currentCaption, 3);

        auto maximumCaption = CCLabelBMFont::create("MAX", "goldFont.fnt");
        maximumCaption->setScale(0.25f);
        maximumCaption->setPosition({217.5f, centerY + 10.0f});
        m_contentLayer->addChild(maximumCaption, 3);

        auto currentValue = CCLabelBMFont::create(
            fmt::format("{:.2f} PTS", m_estimatedScore).c_str(),
            "bigFont.fnt"
        );
        currentValue->setColor(
            m_eligible ? ccc3(90, 235, 255) : ccc3(255, 90, 90)
        );
        currentValue->setPosition({102.5f, centerY - 7.0f});
        currentValue->limitLabelWidth(98.0f, 0.38f, 0.25f);
        m_contentLayer->addChild(currentValue, 3);

        auto maximumValue = CCLabelBMFont::create(
            fmt::format("{:.2f} PTS", m_maximumScore).c_str(),
            "bigFont.fnt"
        );
        maximumValue->setColor(ccc3(255, 220, 95));
        maximumValue->setPosition({217.5f, centerY - 7.0f});
        maximumValue->limitLabelWidth(98.0f, 0.38f, 0.25f);
        m_contentLayer->addChild(maximumValue, 3);
    }

    void onSubmit(CCObject*) {
        if (!corum::ApiClient::submissionAllowed()) {
            corum::ApiClient::showUpdateRequiredWarning();
            return;
        }
        if (!m_eligible || m_state != ViewState::Form) return;

        auto account = GJAccountManager::get();
        auto const username = account ? std::string(account->m_username) : "";
        if (!account || account->m_accountID <= 0 || username.empty()) {
            showFailure("Please sign in to your Geometry Dash account.");
            return;
        }
        m_accountID = account->m_accountID;

        showSubmitting();

        if (m_best >= 100) {
            corum::prepareEvidenceForSubmission(
                m_canonicalLevelID,
                m_sourceLevelID,
                m_accountID,
                [this, username](corum::EvidencePreparationResult result) {
                    if (!result.success) {
                        showFailure(
                            result.error.empty()
                                ? "Could not prepare this record for upload."
                                : result.error
                        );
                        return;
                    }
                    submitRecord(username, std::move(result.evidenceID));
                }
            );
            return;
        }

        submitRecord(username, "");
    }

    void submitRecord(
        std::string const& username,
        std::string evidenceID
    ) {
        m_submissionEvidenceID = std::move(evidenceID);

        auto const now = std::chrono::system_clock::now().time_since_epoch();
        matjson::Value body;
        body["action"] = "record";
        body["levelId"] = m_sourceLevelID;
        body["gdAccountId"] = m_accountID;
        body["gdUsername"] = username;
        body["percent"] = m_best;
        body["attempts"] = m_attempts;
        body["jumps"] = m_jumps;
        body["playTimeMs"] = 0;
        body["platform"] = GEODE_PLATFORM_NAME;
        body["modVersion"] = Mod::get()->getVersion().toVString();
        body["gameVersion"] = GEODE_GD_VERSION_STRING;
        body["geodeVersion"] = Loader::get()->getVersion().toVString();
        body["loadedMods"] = matjson::Value::array();
        for (auto const& loadedMod : corum::ApiClient::loadedMods()) {
            body["loadedMods"].push(loadedMod);
        }
        if (m_best >= 100 && !m_submissionEvidenceID.empty()) {
            body["evidenceId"] = m_submissionEvidenceID;
        }
        body["clientTimestamp"] = static_cast<double>(
            std::chrono::duration_cast<std::chrono::milliseconds>(now).count()
        );

        auto request = web::WebRequest();
        request.header("User-Agent", corum::ApiClient::userAgent());
        request.bodyJSON(body);
        request.followRedirects(false);
        request.timeout(std::chrono::seconds(30));

        m_request.spawn(
            request.post(corum::ApiClient::baseURL()),
            [this](web::WebResponse response) {
                handleInitialResponse(std::move(response));
            }
        );
    }

    void showSubmitting() {
        m_state = ViewState::Submitting;
        clearView();
        setTitle("Submit Record", "goldFont.fnt", 0.78f, 24.0f);
        m_closeBtn->setVisible(false);
        m_closeBtn->setEnabled(false);

        auto spinner = LoadingSpinner::create(66.0f);
        spinner->setPosition({160.0f, 174.0f});
        m_contentLayer->addChild(spinner, 3);

        auto label = CCLabelBMFont::create("Submitting...", "bigFont.fnt");
        label->setScale(0.56f);
        label->setPosition({160.0f, 112.0f});
        m_contentLayer->addChild(label, 3);
    }

    void handleInitialResponse(web::WebResponse response) {
        if (!response.redirected()) {
            handleResponse(std::move(response));
            return;
        }

        auto location = response.header("location");
        if (!location) location = response.header("Location");

        if (!location || location->empty()) {
            showFailure("The server returned an unreadable redirect.");
            return;
        }

        auto request = web::WebRequest();
        request.header("User-Agent", corum::ApiClient::userAgent());
        request.followRedirects(true);
        request.timeout(std::chrono::seconds(30));

        m_request.spawn(
            request.get(std::string(*location)),
            [this](web::WebResponse redirectedResponse) {
                handleResponse(std::move(redirectedResponse));
            }
        );
    }

    void handleResponse(web::WebResponse response) {
        if (response.code() < 200 || response.code() >= 300) {
            auto message = fmt::format(
                "Record submission failed (HTTP {}).",
                response.code()
            );
            if (!response.errorMessage().empty()) {
                message = fmt::format(
                    "{} {}",
                    message,
                    std::string(response.errorMessage()).substr(0, 140)
                );
            }
            showFailure(message);
            return;
        }

        auto root = response.json().unwrapOr(matjson::Value());
        if (!root.isObject()) {
            showFailure("Could not parse the server response.");
            return;
        }

        if (!root["ok"].asBool().unwrapOr(false)) {
            showFailure(responseError(root));
            return;
        }

        if (m_best >= 100 && !m_submissionEvidenceID.empty()) {
            corum::markEvidenceSubmissionComplete(
                m_canonicalLevelID,
                m_accountID
            );
        }
        corum::ApiClient::clearCachedMap(m_levelID, m_accountID);
        auto const playerRegistered =
            root["playerRegistered"].asBool().unwrapOr(false);
        if (root["updated"].asBool().unwrapOr(false)) {
            showSuccess(
                playerRegistered
                    ? "Account registered. Best record and points updated."
                    : "Corum best record and points updated."
            );
        } else if (root["unchanged"].asBool().unwrapOr(false)) {
            showSuccess(
                playerRegistered
                    ? "Account registered. The existing record was kept."
                    : "The server already has an equal or higher record."
            );
        } else {
            showSuccess(
                playerRegistered
                    ? "Account and Corum best record registered."
                    : "Corum best record registered."
            );
        }
    }

    void showSuccess(std::string const& message) {
        showResult(true, "Submission Complete", message);
        notify(message, NotificationIcon::Success);
    }

    void showFailure(std::string const& message) {
        showResult(false, "Submission Failed", message);
        notify(message, NotificationIcon::Error);
    }

    void showResult(
        bool success,
        char const* title,
        std::string const& message
    ) {
        m_state = success ? ViewState::Success : ViewState::Error;
        clearView();
        setTitle(title, "goldFont.fnt", 0.78f, 24.0f);
        m_closeBtn->setVisible(true);
        m_closeBtn->setEnabled(true);

        auto icon = CCSprite::createWithSpriteFrameName(
            success ? "GJ_completesIcon_001.png" : "GJ_deleteIcon_001.png"
        );
        limitNodeSize(icon, {64.0f, 64.0f}, 1.7f, 0.1f);
        icon->setPosition({160.0f, 180.0f});
        m_contentLayer->addChild(icon, 3);

        auto headline = CCLabelBMFont::create(
            success ? "Record submitted" : "Upload failed",
            "bigFont.fnt"
        );
        headline->setScale(0.52f);
        headline->setColor(
            success ? ccc3(125, 255, 145) : ccc3(255, 115, 115)
        );
        headline->setPosition({160.0f, 132.0f});
        m_contentLayer->addChild(headline, 3);

        auto details = SimpleTextArea::create(
            message,
            "bigFont.fnt",
            0.34f,
            236.0f
        );
        details->setAlignment(kCCTextAlignmentCenter);
        details->setColor(ccc4(255, 255, 255, 255));
        details->setMaxLines(3);
        details->setLinePadding(-2.0f);
        details->setPosition({160.0f, 95.0f});
        m_contentLayer->addChild(details, 3);

        auto closeSprite = ButtonSprite::create(
            "Close",
            "bigFont.fnt",
            "GJ_button_01.png",
            0.82f
        );
        closeSprite->setScale(0.78f);

        m_actionButton = CCMenuItemSpriteExtra::create(
            closeSprite,
            this,
            menu_selector(CorumSubmitPopup::onClose)
        );
        m_actionButton->setID("popup-result-close-button"_spr);
        m_actionButton->setPosition({160.0f, 43.0f});
        m_buttonMenu->addChild(m_actionButton);
    }

    void notify(std::string const& message, NotificationIcon icon) {
        if (!Mod::get()->getSettingValue<bool>("submit-notifications")) return;
        Notification::create(message, icon, 3.0f)->show();
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
    static CorumSubmitPopup* create(
        GJGameLevel* level,
        int best
    ) {
        auto popup = new CorumSubmitPopup();
        if (popup && popup->init(level, best)) {
            popup->autorelease();
            return popup;
        }

        delete popup;
        return nullptr;
    }
};

} // namespace

class $modify(CorumLevelInfoLayer, LevelInfoLayer) {
    struct Fields {
        CCDrawNode* ratingCard = nullptr;
        CCNode* rankScoreGroup = nullptr;
        CCLabelBMFont* rankLabel = nullptr;
        CCLabelBMFont* rankScoreSeparator = nullptr;
        CCLabelBMFont* levelScoreLabel = nullptr;
        CCMenu* submitMenu = nullptr;
        CCMenuItemSpriteExtra* submitButton = nullptr;
        std::optional<corum::MapInfo> map;
        int levelID = 0;
        bool listed = false;
    };

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        if (
            !corum::ApiClient::isConfigured() ||
            (!Mod::get()->getSettingValue<bool>("show-rating") &&
             !Mod::get()->getSettingValue<bool>("enable-record-submit")) ||
            m_level->m_levelID <= 0
        ) {
            return true;
        }

        m_fields->levelID = static_cast<int>(m_level->m_levelID);
        corum::ApiClient::initializeSession();

        if (corum::ApiClient::startupReady()) {
            showStartupMap();
        } else if (
            corum::ApiClient::startupStatus() ==
            corum::StartupStatus::Initializing
        ) {
            schedule(
                schedule_selector(CorumLevelInfoLayer::checkStartupStatus),
                0.1f
            );
        }

        return true;
    }

    void checkStartupStatus(float) {
        if (!corum::ApiClient::startupFinished()) return;

        unschedule(
            schedule_selector(CorumLevelInfoLayer::checkStartupStatus)
        );
        if (corum::ApiClient::startupReady()) {
            showStartupMap();
        }
    }

    void showStartupMap() {
        if (m_fields->listed) return;
        if (auto const map = corum::ApiClient::startupMap(m_fields->levelID)) {
            showCorumUI(*map);
        }
    }

    void updateLabelValues() {
        LevelInfoLayer::updateLabelValues();
        if (m_fields->listed) {
            refreshRankScoreLabel();
            refreshSubmitButton();
        }
    }

    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();
        if (m_fields->listed) {
            refreshRankScoreLabel();
            refreshSubmitButton();
        }
    }

    void showCorumUI(corum::MapInfo const& map) {
        m_fields->listed = true;
        m_fields->map = map;

        if (Mod::get()->getSettingValue<bool>("show-rating")) {
            createOrUpdateRatingCard(map);
        }

        refreshSubmitButton();
    }

    void createOrUpdateRatingCard(corum::MapInfo const& map) {
        if (!m_difficultySprite) return;

        if (m_fields->ratingCard) {
            m_fields->ratingCard->removeFromParentAndCleanup(true);
            m_fields->ratingCard = nullptr;
        }
        if (m_fields->rankScoreGroup) {
            m_fields->rankScoreGroup->removeFromParentAndCleanup(true);
            m_fields->rankScoreGroup = nullptr;
            m_fields->rankLabel = nullptr;
            m_fields->rankScoreSeparator = nullptr;
            m_fields->levelScoreLabel = nullptr;
        }

        auto const difficultyPosition = m_difficultySprite->getPosition();
        auto const difficultySize = m_difficultySprite->getScaledContentSize();
        auto const rating = map.rating.empty() ? std::string("?") : map.rating;
        auto const ratingColor = corum::ApiClient::ratingColor(rating);
        constexpr float cardSize = 36.0f;

        auto card = CCDrawNode::create();
        card->setID("rating-card"_spr);
        card->setContentSize({cardSize, cardSize});
        card->setAnchorPoint({0.5f, 0.5f});
        card->drawRect(
            {0.0f, 0.0f},
            {cardSize, cardSize},
            color4(ratingColor),
            2.2f,
            color4(ratingColor)
        );
        card->setPosition({
            difficultyPosition.x - difficultySize.width / 2.0f - cardSize / 2.0f - 7.0f,
            difficultyPosition.y + 2.0f,
        });

        auto value = CCLabelBMFont::create(rating.c_str(), "bigFont.fnt");
        value->setID("rating-value"_spr);
        value->setColor(readableTextColor(ratingColor));
        value->setPosition({cardSize / 2.0f, cardSize / 2.0f});
        value->limitLabelWidth(cardSize - 6.0f, 0.62f, 0.28f);
        card->addChild(value, 2);

        auto rankScoreGroup = CCNode::create();
        rankScoreGroup->setID("rank-score-group"_spr);
        rankScoreGroup->setAnchorPoint({0.5f, 0.5f});
        rankScoreGroup->setPosition({
            difficultyPosition.x,
            difficultyPosition.y + difficultySize.height / 2.0f + 9.0f,
        });

        auto rank = CCLabelBMFont::create("#?", "goldFont.fnt");
        rank->setID("rank-label"_spr);
        rank->setScale(0.42f);

        auto separator = CCLabelBMFont::create("-", "bigFont.fnt");
        separator->setID("rank-score-separator"_spr);
        separator->setScale(0.24f);
        separator->setColor(ccc3(210, 220, 235));

        auto score = CCLabelBMFont::create("0.00 PTS", "bigFont.fnt");
        score->setID("level-score-label"_spr);
        score->setScale(0.32f);

        rankScoreGroup->addChild(rank);
        rankScoreGroup->addChild(separator);
        rankScoreGroup->addChild(score);

        addChild(card, 20);
        addChild(rankScoreGroup, 20);
        m_fields->ratingCard = card;
        m_fields->rankScoreGroup = rankScoreGroup;
        m_fields->rankLabel = rank;
        m_fields->rankScoreSeparator = separator;
        m_fields->levelScoreLabel = score;
        refreshRankScoreLabel();
    }

    void refreshRankScoreLabel() {
        if (
            !m_fields->map ||
            !m_fields->rankScoreGroup ||
            !m_fields->rankLabel ||
            !m_fields->rankScoreSeparator ||
            !m_fields->levelScoreLabel
        ) {
            return;
        }

        auto const& map = *m_fields->map;
        auto const score = corum::baseScore(map.rank);

        m_fields->rankLabel->setString(
            map.rank > 0 ? fmt::format("#{}", map.rank).c_str() : "#?"
        );
        m_fields->rankLabel->setScale(0.42f);

        m_fields->levelScoreLabel->setString(
            fmt::format("{:.2f} PTS", score).c_str()
        );
        m_fields->levelScoreLabel->setColor(ccc3(90, 235, 255));
        m_fields->levelScoreLabel->limitLabelWidth(82.0f, 0.32f, 0.24f);

        auto const rankSize = m_fields->rankLabel->getScaledContentSize();
        auto const separatorSize =
            m_fields->rankScoreSeparator->getScaledContentSize();
        auto const scoreSize =
            m_fields->levelScoreLabel->getScaledContentSize();
        constexpr float gap = 4.0f;
        auto const totalWidth =
            rankSize.width + separatorSize.width + scoreSize.width + gap * 2.0f;
        auto const totalHeight = std::max({
            rankSize.height,
            separatorSize.height,
            scoreSize.height,
        });

        m_fields->rankScoreGroup->setContentSize({totalWidth, totalHeight});
        auto const centerY = totalHeight / 2.0f;
        auto x = 0.0f;

        m_fields->rankLabel->setAnchorPoint({0.0f, 0.5f});
        m_fields->rankLabel->setPosition({x, centerY});
        x += rankSize.width + gap;

        m_fields->rankScoreSeparator->setAnchorPoint({0.0f, 0.5f});
        m_fields->rankScoreSeparator->setPosition({x, centerY});
        x += separatorSize.width + gap;

        m_fields->levelScoreLabel->setAnchorPoint({0.0f, 0.5f});
        m_fields->levelScoreLabel->setPosition({x, centerY});
    }

    void refreshSubmitButton() {
        if (
            !m_fields->listed ||
            !m_fields->map ||
            !Mod::get()->getSettingValue<bool>("enable-record-submit")
        ) {
            removeSubmitButton();
            return;
        }

        if (m_fields->submitMenu && m_fields->submitButton) {
            return;
        }

        auto buttonSprite = CircleButtonSprite::create(
            createPaperPlaneIcon(),
            CircleBaseColor::Green,
            CircleBaseSize::Small
        );
        buttonSprite->setScale(0.86f);

        auto button = CCMenuItemSpriteExtra::create(
            buttonSprite,
            this,
            menu_selector(CorumLevelInfoLayer::onOpenSubmitPopup)
        );
        button->setID("record-submit-button"_spr);

        auto menu = CCMenu::create();
        menu->setID("record-submit-menu"_spr);
        menu->setPosition(CCPointZero);
        menu->setContentSize(CCDirector::sharedDirector()->getWinSize());
        button->setPosition({
            72.0f,
            CCDirector::sharedDirector()->getWinSize().height - 28.0f,
        });
        menu->addChild(button);
        addChild(menu, 20);

        m_fields->submitMenu = menu;
        m_fields->submitButton = button;
    }

    void removeSubmitButton() {
        if (m_fields->submitMenu) {
            m_fields->submitMenu->removeFromParentAndCleanup(true);
        }
        m_fields->submitMenu = nullptr;
        m_fields->submitButton = nullptr;
    }

    void onOpenSubmitPopup(CCObject*) {
        if (
            !m_fields->listed ||
            !m_fields->map ||
            !Mod::get()->getSettingValue<bool>("enable-record-submit")
        ) {
            return;
        }

        if (!corum::ApiClient::submissionAllowed()) {
            corum::ApiClient::showUpdateRequiredWarning();
            return;
        }

        auto const best = std::clamp(m_level->getNormalPercent(), 0, 100);
        auto popup = CorumSubmitPopup::create(m_level, best);
        if (popup) popup->show();
    }
};
