#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace corum::ranked::ui {

// LP shown in the lobby after it changed: each changed digit spins a full turn
// in the direction of the change (right to left, short stagger) and settles.
class RollingNumber final : public cocos2d::CCNode {
    struct Column { cocos2d::CCLabelBMFont* a = nullptr; cocos2d::CCLabelBMFont* b = nullptr; float from = 0, to = 0; bool changed = false; bool hideAtEnd = false; };
    std::vector<Column> m_columns;
    cocos2d::ccColor3B m_color{242, 247, 255}, m_roll{255, 242, 184};
    float m_time = 0.0f, m_delay = 0.35f, m_height = 0.0f;
    bool m_done = false;
    bool init(int from, int to, std::string const& suffix, float scale, cocos2d::ccColor3B color);
public:
    static RollingNumber* create(int from, int to, std::string const& suffix, float scale, cocos2d::ccColor3B color);
    void update(float dt) override;
};

// The lobby card after a tier change: the old emblem and tier name leave
// (promotion: shrink and rise; demotion: sink and fade), a halo blooms and the
// new ones settle in. Driven per frame (no CCEase actions on lobby nodes).
class LobbyTierTransition final : public cocos2d::CCNode {
    geode::Ref<cocos2d::CCNode> m_oldEmblem, m_newEmblem, m_oldName, m_newName, m_bloom;
    cocos2d::CCLayerColor* m_group = nullptr;
    cocos2d::CCPoint m_emblemAt{}, m_nameAt{};
    float m_time = 0.0f;
    bool m_up = true;
    bool init(cocos2d::CCLayerColor* group, cocos2d::CCNode* oldEmblem, cocos2d::CCNode* newEmblem, cocos2d::CCNode* oldName,
        cocos2d::CCNode* newName, cocos2d::CCNode* bloom, bool up);
    void apply();
    void finish();
public:
    static LobbyTierTransition* create(cocos2d::CCLayerColor* group, cocos2d::CCNode* oldEmblem, cocos2d::CCNode* newEmblem,
        cocos2d::CCNode* oldName, cocos2d::CCNode* newName, cocos2d::CCNode* bloom, bool up);
    void update(float dt) override;
};

} // namespace corum::ranked::ui
