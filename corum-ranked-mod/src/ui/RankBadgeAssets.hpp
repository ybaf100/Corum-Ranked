#pragma once

#include <Geode/Geode.hpp>

#include <string>
#include <vector>

namespace corum::ranked::ui {

struct RankTierProfile {
    cocos2d::ccColor3B color;
    cocos2d::ccColor3B secondary;
    int stage = 0;
    int particleCount = 0;
    GLubyte flashOpacity = 0;
    float shake = 0.0f;
};

struct RankBadgePart {
    std::string role;
    cocos2d::CCSprite* sprite = nullptr;
    float assembledScale = 1.0f;
};

// A badge assembled from same-canvas transparent PNG layers. Body/core remain
// separate from ornaments so transition scenes can attach or remove the exact
// tier-defining structure without ever inferring a rank result.
struct RankBadgeAssembly {
    // CCLayerColor implements CCRGBAProtocol consistently on every Geode
    // target, so group fade/cascade actions do not rely on platform-specific
    // CCNode extensions.
    cocos2d::CCLayerColor* root = nullptr;
    std::vector<RankBadgePart> baseParts;
    std::vector<RankBadgePart> ornaments;
    std::vector<RankBadgePart> fragments;
};

enum class RankBadgeDisplay {
    Compact,
    Large,
    Hero,
};

std::string normalizeRankName(std::string rank);
bool isKnownRankName(std::string const& rank);
int rankStageFor(std::string const& rank);
RankTierProfile rankTierProfile(std::string const& rank);

cocos2d::CCSprite* rankResourceSprite(std::string const& filename);
void fitRankSprite(cocos2d::CCSprite* sprite, cocos2d::CCSize maxSize);
void coverRankSprite(cocos2d::CCSprite* sprite, cocos2d::CCSize size);

RankBadgeAssembly createRankBadgeAssembly(std::string const& rank, float badgeSize);
bool hasPersistentRankAmbient(std::string const& rank);
cocos2d::CCNode* createRankAmbientFx(
    std::string const& rank,
    cocos2d::CCSize maxSize,
    RankBadgeDisplay display = RankBadgeDisplay::Compact
);
cocos2d::CCNode* createAnimatedRankBadge(
    std::string const& rank,
    cocos2d::CCSize maxSize,
    RankBadgeDisplay display = RankBadgeDisplay::Compact
);

} // namespace corum::ranked::ui
