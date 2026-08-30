#include "PlacementGradeBadge.hpp"

#include "RankBadgeAssets.hpp"
#include "RankedVisuals.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

std::string normalizeGrade(std::string grade) {
    if (grade.size() != 1) return "D";
    grade[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(grade[0])));
    return grade == "S" || grade == "A" || grade == "B" || grade == "C" || grade == "D"
        ? grade
        : "D";
}

int gradeRank(std::string const& value) {
    auto const grade = normalizeGrade(value);
    if (grade == "S") return 0;
    if (grade == "A") return 1;
    if (grade == "B") return 2;
    if (grade == "C") return 3;
    return 4;
}

std::string gradeFilename(std::string const& grade) {
    auto lower = grade;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return "placement-grade-" + lower + ".png";
}

} // namespace

PlacementGradeTransition placementGradeTransition(
    std::string const& previous,
    std::string const& next
) {
    if (normalizeGrade(previous) == normalizeGrade(next)) return PlacementGradeTransition::None;
    return gradeRank(next) < gradeRank(previous)
        ? PlacementGradeTransition::Up
        : PlacementGradeTransition::Down;
}

PlacementGradeBadge* PlacementGradeBadge::create(float extent) {
    auto* result = new PlacementGradeBadge();
    if (result && result->init(extent)) {
        result->autorelease();
        return result;
    }
    CC_SAFE_DELETE(result);
    return nullptr;
}

bool PlacementGradeBadge::init(float extent) {
    if (!CCNode::init()) return false;
    m_extent = std::max(12.0f, extent);
    setContentSize({m_extent, m_extent});
    setAnchorPoint({0.5f, 0.5f});
    return true;
}

std::string const& PlacementGradeBadge::grade() const {
    return m_grade;
}

CCNode* PlacementGradeBadge::createVisual(std::string const& input) const {
    auto const grade = normalizeGrade(input);
    auto* root = CCNode::create();
    root->setContentSize({m_extent, m_extent});
    root->setAnchorPoint({0.5f, 0.5f});
    root->setPosition({m_extent * 0.5f, m_extent * 0.5f});
    root->setCascadeColorEnabled(true);
    root->setCascadeOpacityEnabled(true);

    if (auto* sprite = rankResourceSprite(gradeFilename(grade))) {
        sprite->setBlendFunc({GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA});
        fitRankSprite(sprite, {m_extent, m_extent});
        sprite->setPosition({m_extent * 0.5f, m_extent * 0.5f});
        root->addChild(sprite);
    } else {
        // Missing image resources are presentation-only failures. The same node
        // and animation path remains available through a single-letter fallback.
        auto* fallback = label(
            grade,
            1.0f,
            {m_extent * 0.5f, m_extent * 0.5f},
            Text,
            FontWeight::Black
        );
        fallback->limitLabelWidth(m_extent * 0.82f, 1.0f, 0.2f);
        root->addChild(fallback);
    }
    return root;
}

void PlacementGradeBadge::setGrade(std::string input, bool animate) {
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
}

} // namespace corum::ranked::ui
