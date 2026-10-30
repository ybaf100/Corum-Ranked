#include "RankedRuntime.hpp"
#include "domain/HudPresentation.hpp"
#include "domain/CompetitiveHudLayout.hpp"
#include "domain/ScoreDisplay.hpp"
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
constexpr float kNormalDeltaPairMinOffset = 34.0f;
constexpr float kHudShadowOffsetX = 0.75f;
constexpr float kHudShadowOffsetY = -0.75f;
constexpr ccColor3B kPositiveDeltaLight = {184, 246, 207};
constexpr ccColor3B kNegativeDeltaLight = {255, 188, 196};

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
    return corum::ranked::soloScoreText(value);
}

std::string formatDeathmatchDelta(double value) {
    auto const magnitude = std::abs(value);
    auto const precision = magnitude >= 5.0
        ? 0
        : magnitude >= 1.0
            ? 1
            : 2;

    std::ostringstream stream;
    if (value > 0.0) stream << '+';
    stream << std::fixed << std::setprecision(precision) << value;
    return stream.str();
}

ccColor3B blendColor(ccColor3B from, ccColor3B to, double ratio) {
    auto const t = std::clamp(ratio, 0.0, 1.0);
    auto const component = [t](GLubyte a, GLubyte b) {
        return static_cast<GLubyte>(std::lround(
            static_cast<double>(a) +
            (static_cast<double>(b) - static_cast<double>(a)) * t
        ));
    };
    return {
        component(from.r, to.r),
        component(from.g, to.g),
        component(from.b, to.b),
    };
}

ccColor3B deltaColor(double value, double fullColorAt) {
    if (value == 0.0 || fullColorAt <= 0.0) {
        return corum::ranked::ui::TextMuted;
    }
    auto const positive = value > 0.0;
    auto const start = positive ? kPositiveDeltaLight : kNegativeDeltaLight;
    auto const target = positive
        ? corum::ranked::ui::Green
        : corum::ranked::ui::Red;
    return blendColor(
        start,
        target,
        std::abs(value) / fullColorAt
    );
}

std::string signedValue(double value, std::string const& suffix) {
    return corum::ranked::soloScoreText(value,true)+suffix;
}

float renderedWidth(CCLabelBMFont* label) {
    if (!label) return 0.0f;
    return label->getContentSize().width * std::abs(label->getScaleX());
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
    int zOrder,
    corum::ranked::ui::FontWeight weight = corum::ranked::ui::FontWeight::SemiBold
) {
    ShadowLabel result;
    result.shadow = corum::ranked::ui::label(
        text,
        scale,
        {position.x + kHudShadowOffsetX, position.y + kHudShadowOffsetY},
        {0, 0, 0},
        weight
    );
    result.shadow->setAnchorPoint(anchor);
    result.shadow->setOpacity(205);
    parent->addChild(result.shadow, zOrder - 1);

    result.foreground = corum::ranked::ui::label(
        text,
        scale,
        position,
        color,
        weight
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
    if (label.shadow) {
        label.shadow->setPosition({
            position.x + kHudShadowOffsetX,
            position.y + kHudShadowOffsetY,
        });
    }
}

void setShadowColor(ShadowLabel const& label, ccColor3B color) {
    if (label.foreground) label.foreground->setColor(color);
}

CCLabelBMFont* findHudShadow(CCLabelBMFont* foreground) {
    if (!foreground) return nullptr;
    auto* parent = foreground->getParent();
    if (!parent || !parent->getChildren()) return nullptr;
    auto const position = foreground->getPosition();
    auto const target = CCPoint{
        position.x + kHudShadowOffsetX,
        position.y + kHudShadowOffsetY,
    };
    for (auto* node : CCArrayExt<CCNode*>(parent->getChildren())) {
        auto* candidate = typeinfo_cast<CCLabelBMFont*>(node);
        if (!candidate || candidate == foreground) continue;
        auto const candidatePosition = candidate->getPosition();
        if (
            std::abs(candidatePosition.x - target.x) <= 0.25f &&
            std::abs(candidatePosition.y - target.y) <= 0.25f
        ) {
            return candidate;
        }
    }
    return nullptr;
}

void setExternalShadowedPosition(CCLabelBMFont* foreground, CCPoint position) {
    if (!foreground) return;
    auto* shadow = findHudShadow(foreground);
    foreground->setPosition(position);
    if (shadow) {
        shadow->setPosition({
            position.x + kHudShadowOffsetX,
            position.y + kHudShadowOffsetY,
        });
    }
}

void setExternalShadowedText(
    CCLabelBMFont* foreground,
    std::string const& text,
    ccColor3B color
) {
    if (!foreground) return;
    auto* shadow = findHudShadow(foreground);
    foreground->setString(text.c_str());
    foreground->setColor(color);
    if (shadow) shadow->setString(text.c_str());
}

} // namespace

// Spectator authority/safety lives in PlayLayer's explicit local-dummy policy.
// No frame temporarily changes m_isPracticeMode. User Practice remains a
// separate vanilla toggle + Ranked presence/scoring transition.

class $modify(CorumRankedGameplayUx, PlayLayer) {
    struct Fields {
        ShadowLabel scoreDelta;
        ShadowLabel attemptsLeft;
        ShadowLabel opponentAttemptsLeft;
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
            // Spectator dummy safety never enters this user-toggle path.
            log::info("Ranked Practice Mode ignored during Death Match");
            return;
        }
        PlayLayer::togglePracticeMode(practiceMode);
    }

    CCLabelBMFont* timerLabel() {
        return typeinfo_cast<CCLabelBMFont*>(getChildByIDRecursive("ranked-countdown"_spr));
    }

    void ensureLabels() {
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const layout = corum::ranked::layoutHud(size.width, size.height);
        auto* timer = timerLabel();

        if (!m_fields->scoreDelta.foreground && timer && timer->getParent()) {
            m_fields->scoreDelta = createShadowLabel(
                timer->getParent(),
                "0",
                corum::ranked::CompetitiveClockScale,
                {0.5f, 1.0f},
                {size.width / 2.0f + kNormalDeltaPairMinOffset, timer->getPositionY()},
                corum::ranked::ui::TextMuted,
                4,
                corum::ranked::ui::FontWeight::ExtraBold
            );
            m_fields->scoreDelta.foreground->setID("ranked-score-delta"_spr);
            setShadowVisible(m_fields->scoreDelta, false);
        }
        if (!m_fields->attemptsLeft.foreground) {
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
        if (!m_fields->opponentAttemptsLeft.foreground) {
            m_fields->opponentAttemptsLeft = createShadowLabel(
                this,
                "",
                0.34f,
                {1.0f, 1.0f},
                {layout.topRightX - 8.0f, layout.topY - 53.0f},
                corum::ranked::ui::Text,
                1002
            );
            m_fields->opponentAttemptsLeft.foreground->setID(
                "ranked-deathmatch-opponent-attempts-left"_spr
            );
            setShadowVisible(m_fields->opponentAttemptsLeft, false);
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

    void restoreTimerCenter() {
        auto* timer = timerLabel();
        if (!timer) return;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        setExternalShadowedPosition(timer, {size.width / 2.0f, timer->getPositionY()});
    }

    void positionNormalTimerPair() {
        auto* timer = timerLabel();
        if (!timer || !m_fields->scoreDelta.foreground) return;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        auto const y = timer->getPositionY();
        auto const pair=corum::ranked::competitiveClockPair(size.width/2,renderedWidth(timer),renderedWidth(m_fields->scoreDelta.foreground));
        setExternalShadowedPosition(
            timer,
            {pair.clockX, y}
        );
        setShadowPosition(
            m_fields->scoreDelta,
            {pair.deltaX, y}
        );
    }

    void updateNormalScoreDelta() {
        auto& runtime = corum::ranked::RankedRuntime::get();
        auto const& match = runtime.view().match;
        if (runtime.isPlacement() && !runtime.isSpectating() && m_fields->scoreDelta.foreground &&
            (match.state == "MAP_PLAYING" || match.state == "MAP_SETTLING")) {
            auto const score = runtime.localDisplayScore(m_isPracticeMode ? -1.0 : currentProgress(this));
            auto const grade = runtime.placementDisplayGrade(score);
            setShadowText(m_fields->scoreDelta, formatNumber(score));
            auto const color = grade == "D" ? ccc3(92, 92, 98) : grade == "C" ? ccc3(205, 127, 50) :
                grade == "B" ? ccc3(205, 218, 234) : grade == "A" ? ccc3(255, 211, 70) : ccc3(255, 255, 255);
            setShadowColor(m_fields->scoreDelta, color);
            if (grade == "S") {
                // BMFont glyphs retain native rendering and the black shadow.
                // Spread the rainbow across the score, with no new image pack.
                constexpr std::array<ccColor3B, 6> rainbow {{{255, 89, 111}, {255, 177, 67},
                    {255, 229, 79}, {78, 234, 145}, {79, 191, 255}, {189, 116, 255}}};
                auto* foreground = m_fields->scoreDelta.foreground;
                auto const width = std::max(1.0f, foreground->getContentSize().width);
                for (auto* child : foreground->getChildrenExt()) {
                    auto* glyph = typeinfo_cast<CCSprite*>(child); if (!glyph) continue;
                    auto const position = std::clamp(glyph->getPositionX() / width, 0.0f, 1.0f) * 5;
                    auto const index = std::min(4, static_cast<int>(position));
                    auto const fraction = position - index;
                    auto const& a = rainbow[index]; auto const& b = rainbow[index + 1];
                    glyph->setColor(ccc3(static_cast<GLubyte>(a.r + (b.r - a.r) * fraction),
                        static_cast<GLubyte>(a.g + (b.g - a.g) * fraction),
                        static_cast<GLubyte>(a.b + (b.b - a.b) * fraction)));
                }
            }
            positionNormalTimerPair(); setShadowVisible(m_fields->scoreDelta, true); return;
        }
        auto const normalState =
            match.state == "ROUND_PLAYING" ||
            match.state == "FINAL_ATTEMPT_WINDOW" ||
            match.state == "LAST_ATTEMPT_WINDOW" ||
            match.state == "ROUND_SETTLING";
        if (
            !normalState || runtime.isPlacement() || runtime.isSpectating() ||
            !m_fields->scoreDelta.foreground
        ) {
            setShadowVisible(m_fields->scoreDelta, false);
            restoreTimerCenter();
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
        setShadowText(m_fields->scoreDelta, signedValue(difference, ""));
        setShadowColor(m_fields->scoreDelta, deltaColor(difference, 200.0));
        positionNormalTimerPair();
        setShadowVisible(m_fields->scoreDelta, true);
    }

    void overrideDeathmatchDelta(std::string const& text, ccColor3B color) {
        auto* timer = timerLabel();
        if (!timer) return;
        auto const size = CCDirector::sharedDirector()->getWinSize();
        setExternalShadowedPosition(timer, {size.width / 2.0f, timer->getPositionY()});
        setExternalShadowedText(timer, text, color);
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
        auto const ownIsA = match.side == "A";
        auto const ownCommitted = ownIsA ? match.committedScoreA : match.committedScoreB;
        auto const opponentCommitted = ownIsA ? match.committedScoreB : match.committedScoreA;
        auto const ownServerDisplay = ownIsA ? match.scoreA : match.scoreB;
        auto const opponentServerDisplay = ownIsA ? match.scoreB : match.scoreA;
        auto const ownProgress = corum::ranked::deathmatchIconProgress(
            ownCommitted,
            ownServerDisplay,
            ownActive ? std::optional<double>(currentProgress(this)) : std::nullopt
        );
        auto const opponentProgress = corum::ranked::deathmatchIconProgress(
            opponentCommitted,
            opponentServerDisplay,
            opponentActive && match.opponentProgress
                ? std::optional<double>(static_cast<double>(*match.opponentProgress))
                : std::nullopt
        );
        auto const close = std::abs(ownProgress - opponentProgress) <= 4.0;

        if (m_fields->ownProgressIcon) {
            m_fields->ownProgressIcon->setVisible(true);
            {
                m_fields->ownProgressIcon->setPosition({
                    startX + width * static_cast<float>(ownProgress / 100.0),
                    barY + (close ? 6.0f : 0.0f),
                });
            }
        }
        if (m_fields->opponentProgressIcon) {
            m_fields->opponentProgressIcon->setVisible(true);
            {
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
        auto const ownAttemptsLeft = std::clamp(3 - ownCompleted, 0, 3);
        auto const opponentAttemptsLeft = std::clamp(3 - opponentCompleted, 0, 3);

        auto const ownPrefix = corum::ranked::i18n::literal("inline.attempts-left");
        auto const opponentPrefix = corum::ranked::i18n::literal("inline.opp-attempts-left");
        setShadowText(
            m_fields->attemptsLeft,
            ownPrefix + std::to_string(ownAttemptsLeft)
        );
        setShadowColor(
            m_fields->attemptsLeft,
            ownAttemptsLeft <= 1 ? corum::ranked::ui::Gold : corum::ranked::ui::Text
        );
        setShadowVisible(m_fields->attemptsLeft, true);

        setShadowText(
            m_fields->opponentAttemptsLeft,
            opponentPrefix + std::to_string(opponentAttemptsLeft)
        );
        setShadowColor(
            m_fields->opponentAttemptsLeft,
            opponentAttemptsLeft <= 1 ? corum::ranked::ui::Gold : corum::ranked::ui::Text
        );
        setShadowVisible(m_fields->opponentAttemptsLeft, true);

        auto const ownActive = runtime.hasLocalAttemptInFlight() && !runtime.isSpectating();
        auto const opponentActive =
            opponentUsed > opponentCompleted &&
            !match.opponentFinished &&
            match.opponentProgress.has_value();

        // Death Match score is best raw progress, not the current attempt alone.
        // Use the authoritative score, which remains formatted by the main Ranked
        // HUD with up to three decimal places, and only reduce precision for the
        // central +/- difference readout.
        auto ownDisplayScore = ownIsA ? match.scoreA : match.scoreB;
        auto opponentDisplayScore = ownIsA ? match.scoreB : match.scoreA;
        if (ownActive) {
            ownDisplayScore = std::max(ownDisplayScore, currentProgress(this));
        }
        if (match.opponentProgress.has_value()) {
            opponentDisplayScore = std::max(
                opponentDisplayScore,
                static_cast<double>(std::clamp(match.opponentProgress.value(), 0, 100))
            );
        }
        auto const difference = ownDisplayScore - opponentDisplayScore;
        overrideDeathmatchDelta(
            formatDeathmatchDelta(difference),
            deltaColor(difference, 200.0)
        );

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
            setShadowVisible(m_fields->opponentAttemptsLeft, false);
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
        setShadowVisible(m_fields->opponentAttemptsLeft, false);
        hideDeathmatchIcons();
        updateNormalScoreDelta();
    }
};
