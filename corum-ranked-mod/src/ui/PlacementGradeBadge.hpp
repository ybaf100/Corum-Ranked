#pragma once

#include <Geode/Geode.hpp>

#include <string>

namespace corum::ranked::ui {

enum class PlacementGradeTransition {
    None,
    Up,
    Down,
};

PlacementGradeTransition placementGradeTransition(
    std::string const& previous,
    std::string const& next
);

class PlacementGradeBadge final : public cocos2d::CCNode {
public:
    static PlacementGradeBadge* create(float extent = 58.0f);

    bool init(float extent);
    void setGrade(std::string grade, bool animate = true);
    [[nodiscard]] std::string const& grade() const;

private:
    cocos2d::CCNode* createVisual(std::string const& grade) const;

    float m_extent = 58.0f;
    std::string m_grade;
    cocos2d::CCNode* m_visual = nullptr;
};

} // namespace corum::ranked::ui
