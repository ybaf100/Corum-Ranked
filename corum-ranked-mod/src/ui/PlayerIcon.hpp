#pragma once

#include "../domain/PlayerIconResolution.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/UserInfoDelegate.hpp>

class GJUserScore;
class SimplePlayer;

namespace corum::ranked::ui {

class PlayerIcon final : public cocos2d::CCNode, public UserInfoDelegate {
public:
    static PlayerIcon* createLocal();
    static PlayerIcon* createBot(int index);
    static PlayerIcon* createRemote(int accountId);

    ~PlayerIcon() override;
    void onEnter() override;
    void onExit() override;

    void getUserInfoFinished(GJUserScore* score) override;
    void getUserInfoFailed(int accountId) override;
    void userInfoChanged(GJUserScore* score) override;
    // Paint the icon gold (Slice executor); survives a late remote icon load.
    void setGold(bool gold);
    // Opacity of the drawn icon (afterimage trails).
    void setIconOpacity(GLubyte opacity);

private:
    bool initLocal();
    bool initRemote(int accountId);
    bool initFallback();
    void applyLocalCosmetics();
    void applyRemoteCosmetics(GJUserScore* score);
    void resolveRemote(float);
    void releaseDelegate();

    corum::ranked::PlayerIconResolution m_resolution {0};
    SimplePlayer* m_icon = nullptr;
    bool m_local = false;
    bool m_requestInFlight = false;
    bool m_gold = false;
    void applyGold();
};

} // namespace corum::ranked::ui
