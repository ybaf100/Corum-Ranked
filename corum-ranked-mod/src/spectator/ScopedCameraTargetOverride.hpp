#pragma once
#include <Geode/binding/PlayerObject.hpp>
#include <optional>

namespace corum::ranked {
// Camera calculation ONLY. Bypass PlayerObject/GameObject::setPosition so no
// collision, last-position, dirty geometry or gameplay bookkeeping is touched.
// The two native coordinate mirrors are restored independently of node position.
class ScopedCameraTargetOverride final {
public:
    ScopedCameraTargetOverride(PlayerObject* player, std::optional<cocos2d::CCPoint> const& target)
        : m_player(target ? player : nullptr) {
        if (!m_player) return;
        m_position = m_player->getPosition();
        m_x = m_player->m_positionX; m_y = m_player->m_positionY;
        m_player->cocos2d::CCNode::setPosition(*target);
        m_player->m_positionX = target->x; m_player->m_positionY = target->y;
    }
    ~ScopedCameraTargetOverride() {
        if (!m_player) return;
        m_player->cocos2d::CCNode::setPosition(m_position);
        m_player->m_positionX = m_x; m_player->m_positionY = m_y;
    }
    ScopedCameraTargetOverride(ScopedCameraTargetOverride const&) = delete;
    ScopedCameraTargetOverride& operator=(ScopedCameraTargetOverride const&) = delete;
private:
    PlayerObject* m_player;
    cocos2d::CCPoint m_position;
    double m_x = 0, m_y = 0;
};
} // namespace corum::ranked
