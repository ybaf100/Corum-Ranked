#include "RankedRuntime.hpp"
#include "ui/PlayerIcon.hpp"
#include "ui/RankedTranslations.hpp"
#include "ui/RankedVisuals.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/utils/cocos.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>

using namespace geode::prelude;

namespace {

constexpr float kDeltaEpsilon = 0.0005f;

bool isRankedGameplayLevel(PlayLayer* layer) {
    if (!layer || !layer->m_level) return false;
    auto& runtime = corum::ranked::RankedRuntime::get();
    return
        runtime.view().stage == corum::ranked::RuntimeStage::Matched &&
        runtime.isGameplayLevel(static_cast<int>(layer->m_level->m_levelID));
}

double currentProgress(PlayLayer* layer) {
    if (!layer) return 0.0;
    auto const precise = std::clamp(static_cast<double>(layer->getCurrentPercent()), 0.0, 100.0);
    auto const whole = std::clamp(static_cast<double>(layer->getCurrentPercentInt()), 0.0, 100.0);
    return std::max(precise, whole);
}

std::string formatNumber(double value) {
    if (std::abs(value - std::round(value)) < 0.0005) {
        return std::to_string(static_cast<long long>(std::llround(value)));
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << value;
    auto text = stream.str();
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text;
}

ccColor3B deltaColor(double value) {
    if (value > kDeltaEpsilon) return corum::ranked::ui::Green;
    if (value < -kDeltaEpsilon) return corum::ranked::ui::Red;
    return corum::ranked::ui::TextMuted;
}

std::string signedValue(double value, std::string const& suffix) {
    auto const normalized = std::abs(value) < 0.0005 ? 0.0 : value;
    auto text = formatNumber(normalized);
    if (normalized > 0.0) text.insert(text.begin(), '+');
    return text + suffix;
}

struct ShadowLabel {
    CCLabelBMFont* foreground = nullptr;
    CCLabelBMFont* shadow = nullptr;
};

ShadowLabel createShadowLabel(
    CCNode* parent,
    std::string const& text,
    float scale,
    CCPoint anchor,
    CCPoint position,
    ccColor3B color,
    int zOrder
) {
    ShadowLabel result;
    result.shadow = corum::ranked::ui::label(
        text,
        scale,
        {position.x + 0.75f, position.y - 0.75f},
        {0, 0, 0},
        corum::ranked::ui::FontWeight::SemiBold
    );
    result.shadow->setAnchorPoint(anchor);
    result.shadow->setOpacity(205);
    parent->addChild(result.shadow, zOrder - 1);

    result.foreground = corum::ranked::ui::label(
        text,
        scale,
        position,
        color,
        corum::ranked::ui::FontWeight::SemiBold
    );
    result.foreground->setAnchorPoint(anchor);
    parent->addChild(result.foreground, zOrder);
    return result;
}

void setShadowText(ShadowLabel const& label, std::string const& text) {
    if (label.foreground) label.foreground->setString(text.c_str());
    if (label.shadow) label.shadow->setString(text.c_str());
}

void setShadowVisible(ShadowLabel const& label, bool visible) {
    if (label.foreground) label.foreground->setVisible(visible);
    if (label.shadow) label.shadow->setVisible(visible);
}

void setShadowPosition(ShadowLabel const& label, CCPoint position) {
    if (label.foreground) label.foreground->setPosition(position);
    if (label.shadow) label.shadow->setPosition({position.x + 0.75f, position.y - 0.75f});
}

void setShadowColor(ShadowLabel const& label, ccColor3B color) {
    if (label.foreground) label.foreground->setColor(color);
}

} // namespace

// This hook is intentionally inside the existing Corum Ranked postUpdate hook.
// It exposes Practice semantics only to vanilla gameplay for the duration of the
// original frame. The flag is restored before Ranked's own Practice/timeout
// logic runs, so read-only spectating can never be mistaken for user Practice
// and auto-exit because the Ranked start window has ended.
class $modify(CorumRankedSpectatorPracticeGuard, PlayLayer) {
    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PlayLayer::postUpdate", Priority::Late)) {
            log::warn("Failed to install spectator Practice frame guard priority");
        }
    }

    void postUpdate(float dt) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const spectatorPractice = isRankedGameplayLevel(this) && runtime.isSpectating();
        auto const previousPractice = m_isPracticeMode;
        if (spectatorPractice) m_isPracticeMode = true;
        PlayLayer::postUpdate(dt);
        if (spectatorPractice) m_isPracticeMode = previousPractice;
    }
};

class $modify(CorumRankedGameplayUx, PlayLayer) {
    struct Fields {
        ShadowLabel scoreDelta;
        ShadowLabel attemptsLeft;
        corum::ranked::ui::PlayerIcon* ownProgressIcon = nullptr;
        corum::ranked::ui::PlayerIcon* opponentProgressIcon = nullptr;
        int opponentIconAccountId = 0;
    };

    static void onModify(auto& self) {
        if (!self.setHookPriorityPre("PlayLayer::postUpdate", Priority::First)) {
            log::warn("Failed to install Ranked gameplay UX postUpdate priority");
        }
        if (!self.setHookPriorityPre("PlayLayer::togglePracticeMode", Priority::First)) {
            log::warn("Failed to install Death Match Practice guard priority");
        }
    }

    void postUpdate(float dt) {
        // Run after every existing hook has updated RankedRuntime/HUD. This hook is
        // Priority::First, so code after the chained call is the final presentation
        // pass for the frame.
        PlayLayer::postUpdate(dt);
        updateCompetitiveUx();
    }

    void togglePracticeMode(bool practiceMode) {
        auto& runtime = corum::ranked::RankedRuntime::get();
        if (
            practiceMode &&
            isRankedGameplayLevel(this) &&
            runtime.view().match.state == "DEATHMATCH_PLAYING"
        ) {
            // Death Match is raw best-progress competition with exactly three
            // authoritative Attempts. User Practice is therefore unavailable.
            // Spectator read-only Practice is implemented by the separate frame
            // guard above and never enters this user-toggle path.
            log::info("Ranked Practice Mode ignored during Death Match");
            return;
        }
        PlayLayer::togglePracticeMode(practiceMode);
    }

    void ensureLabels() {
        if (!m_fields->scoreDelta.foreground) {
            m_fields->scoreDelta = createShadowLabel(
                this,
                "0",
                0.31f,
                {0.5f, 0.5f},
                {0.0f, 0.0f},
                corum::ranked::ui::TextMuted,
                1002
            );
            m_fields->scoreDelta.foreground->setID("ranked-score-delta"_spr);
            setShadowVisible(m_fields->scoreDelta, false);
        }
        if (!m_fields->attemptsLeft.foreground) {
            auto const size = CCDirector::sharedDirector()->getWinSize();
            auto const layout = corum::ranked::layoutHud(size.width, size.height);
            m_fields->attemptsLeft = createShadowLabel(
                this,
                "",
                0.34f,
                {0.0f, 1.0f},
                {layout.topLeftX + 8.0f, layout.topY - 53.0f},
                corum::ranked::ui::Text,
                1002
            );
            m_fields->attemptsLeft.foreground->setID("ranked-deathmatch-attempts-left"_spr);
            setShadowVisible(m_fields->attemptsLeft, false);
        }
    }

    void ensureProgressIcons() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        if (!m_fields->ownProgressIcon) {
            m_fields->ownProgressIcon = corum::ranked::ui::PlayerIcon::createLocal();
            if (m_fields->ownProgressIcon) {
                m_fields->ownProgressIcon->setID("ranked-deathmatch-own-progress-icon"_spr);
                m_fields->ownProgressIcon->setScale(0.32f);
                m_fields->ownProgressIcon->setVisible(false);
                addChild(m_fields->ownProgressIcon, 1003);
            }
        }
        auto const opponentAccountId = match.side == "A"
            ? match.playerBAccountId
            : match.playerAAccountId;
        if (!m_fields->opponentProgressIcon || m_fields->opponentIconAccountId != opponentAccountId) {
            if (m_fields->opponentProgressIcon) {
                m_fields->opponentProgressIcon->removeFromParentAndCleanup(true);
                m_fields->opponentProgressIcon = nullptr;
            }
            m_fields->opponentIconAccountId = opponentAccountId;
            m_fields->opponentProgressIcon =
                corum::ranked::ui::PlayerIcon::createRemote(opponentAccountId);
            if (m_fields->opponentProgressIcon) {
                m_fields->opponentProgressIcon->setID("ranked-deathmatch-opponent-progress-icon"_spr);
                m_fields->opponentProgressIcon->setScale(0.32f);
                m_fields->opponentProgressIcon->setVisible(false);
                addChild(m_fields->opponentProgressIcon, 1003);
            }
        }
    }

    void hideDeathmatchIcons() {
        if (m_fields->ownProgressIcon) m_fields->ownProgressIcon->setVisible(false);
        if (m_fields->opponentProgressIcon) m_fields->opponentProgressIcon->setVisible(false);
    }

    void updateNormalScoreDelta() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        auto* percentage = typeinfo_cast<CCLabelBMFont*>(getChildByID("percentage-label"));
        auto const normalState =
            match.state == "ROUND_PLAYING" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "ROUND_SETTLING";
        if (
            !percentage || !percentage->isVisible() || !normalState ||
            runtime.isPlacement() || runtime.isSpectating()
        ) {
            setShadowVisible(m_fields->scoreDelta, false);
            return;
        }

        auto const ownIsA = match.side == "A";
        auto ownScore = ownIsA ? match.scoreA : match.scoreB;
        auto const opponentScore = ownIsA ? match.scoreB : match.scoreA;
        if (runtime.hasLocalAttemptInFlight() && !m_isPracticeMode) {
            auto const qualifying = match.currentMap
                ? std::optional<double>(match.currentMap->qualifyingPercent)
                : std::nullopt;
            ownScore = runtime.localDisplayScore(currentProgress(this), qualifying);
        }
        auto const difference = ownScore - opponentScore;
        auto const suffix = corum::ranked::i18n::isKorean() ? "점" : " PTS";
        setShadowText(m_fields->scoreDelta, signedValue(difference, suffix));
        setShadowColor(m_fields->scoreDelta, deltaColor(difference));
        setShadowPosition(
            m_fields->scoreDelta,
            {percentage->getPositionX(), percentage->getPositionY() - 10.0f}
        );
        setShadowVisible(m_fields->scoreDelta, true);
    }

    void overrideDeathmatchDelta(std::string const& text, ccColor3B color) {
        auto* timer = typeinfo_cast<CCLabelBMFont*>(getChildByIDRecursive("ranked-countdown"_spr));
        if (!timer) return;
        timer->setString(text.c_str());
        timer->setColor(color);

        // Ranked's compact HUD gives every foreground label a black shadow at a
        // fixed +0.75/-0.75 offset. Update that sibling as well so the old
        // remaining-attempt digit cannot remain visible behind the new delta.
        auto* parent = timer->getParent();
        if (!parent || !parent->getChildren()) return;
        auto const target = CCPoint{
            timer->getPositionX() + 0.75f,
            timer->getPositionY() - 0.75f,
        };
        for (auto* node : CCArrayExt<CCNode*>(parent->getChildren())) {
            auto* candidate = typeinfo_cast<CCLabelBMFont*>(node);
            if (!candidate || candidate == timer) continue;
            auto const position = candidate->getPosition();
            if (
                std::abs(position.x - target.x) <= 0.25f &&
                std::abs(position.y - target.y) <= 0.25f
            ) {
                candidate->setString(text.c_str());
                break;
            }
        }
    }

    void updateDeathmatchProgressIcons(bool ownActive, bool opponentActive) {
        ensureProgressIcons();
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        auto* progressBar = typeinfo_cast<CCSprite*>(getChildByID("progress-bar"));
        if (!progressBar || !progressBar->isVisible()) {
            hideDeathmatchIcons();
            return;
        }

        auto const width = progressBar->getContentSize().width * std::abs(progressBar->getScaleX());
        if (width <= 1.0f) {
            hideDeathmatchIcons();
            return;
        }
        auto const startX = progressBar->getPositionX() -
            width * progressBar->getAnchorPoint().x;
        auto const barY = progressBar->getPositionY();
        auto const ownProgress = currentProgress(this);
        auto const opponentProgress = static_cast<double>(std::clamp(
            match.opponentProgress.value_or(0),
            0,
            100
        ));
        auto const close = ownActive && opponentActive &&
            std::abs(ownProgress - opponentProgress) <= 4.0;

        if (m_fields->ownProgressIcon) {
            m_fields->ownProgressIcon->setVisible(ownActive);
            if (ownActive) {
                m_fields->ownProgressIcon->setPosition({
                    startX + width * static_cast<float>(ownProgress / 100.0),
                    barY + (close ? 6.0f : 0.0f),
                });
            }
        }
        if (m_fields->opponentProgressIcon) {
            m_fields->opponentProgressIcon->setVisible(opponentActive);
            if (opponentActive) {
                m_fields->opponentProgressIcon->setPosition({
                    startX + width * static_cast<float>(opponentProgress / 100.0),
                    barY + (close ? -6.0f : 0.0f),
                });
            }
        }
    }

    void updateDeathmatchUx() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        auto const ownIsA = match.side == "A";
        auto const ownCompleted = ownIsA
            ? match.deathmatchAttemptsCompletedA
            : match.deathmatchAttemptsCompletedB;
        auto const opponentUsed = ownIsA
            ? match.deathmatchAttemptsUsedB
            : match.deathmatchAttemptsUsedA;
        auto const opponentCompleted = ownIsA
            ? match.deathmatchAttemptsCompletedB
            : match.deathmatchAttemptsCompletedA;
        auto const attemptsLeft = std::clamp(3 - ownCompleted, 0, 3);
        auto const prefix = corum::ranked::i18n::isKorean()
            ? "남은 시도 : "
            : "ATTEMPTS LEFT : ";
        setShadowText(m_fields->attemptsLeft, prefix + std::to_string(attemptsLeft));
        setShadowColor(m_fields->attemptsLeft, attemptsLeft <= 1
            ? corum::ranked::ui::Gold
            : corum::ranked::ui::Text);
        setShadowVisible(m_fields->attemptsLeft, true);

        auto const ownActive = runtime.hasLocalAttemptInFlight() && !runtime.isSpectating();
        auto const opponentActive =
            opponentUsed > opponentCompleted &&
            !match.opponentFinished &&
            match.opponentProgress.has_value();

        if (ownActive && opponentActive) {
            auto const difference = static_cast<int>(std::lround(
                currentProgress(this) -
                static_cast<double>(match.opponentProgress.value_or(0))
            ));
            auto const text = difference > 0
                ? "+" + std::to_string(difference) + "%"
                : std::to_string(difference) + "%";
            overrideDeathmatchDelta(text, deltaColor(static_cast<double>(difference)));
        } else {
            overrideDeathmatchDelta("-", corum::ranked::ui::TextMuted);
        }

        if (auto* oldProgress = getChildByIDRecursive("ranked-opponent-progress-hud"_spr)) {
            oldProgress->setVisible(false);
        }
        updateDeathmatchProgressIcons(ownActive, opponentActive);
    }

    void updateCompetitiveUx() {
        ensureLabels();
        if (!isRankedGameplayLevel(this)) {
            setShadowVisible(m_fields->scoreDelta, false);
            setShadowVisible(m_fields->attemptsLeft, false);
            hideDeathmatchIcons();
            return;
        }

        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const deathmatch = runtime.view().match.state == "DEATHMATCH_PLAYING";
        if (deathmatch) {
            setShadowVisible(m_fields->scoreDelta, false);
            updateDeathmatchUx();
            return;
        }

        setShadowVisible(m_fields->attemptsLeft, false);
        hideDeathmatchIcons();
        updateNormalScoreDelta();
    }
};
