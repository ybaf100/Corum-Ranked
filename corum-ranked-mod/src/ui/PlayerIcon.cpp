#include "PlayerIcon.hpp"

#include <Geode/binding/GameLevelManager.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJUserScore.hpp>
#include <Geode/binding/SimplePlayer.hpp>

#include <algorithm>

using namespace geode::prelude;

namespace corum::ranked::ui {

PlayerIcon* PlayerIcon::createLocal() {
    auto* result = new PlayerIcon();
    if (result && result->initLocal()) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

PlayerIcon* PlayerIcon::createBot(int index) {
    auto* node=new PlayerIcon;if(!node->initFallback()){delete node;return nullptr;}
    node->autorelease();index=std::clamp(index,1,15);
    node->m_icon->updatePlayerFrame(index,IconType::Cube);
    constexpr ccColor3B colors[]={{79,230,250},{255,211,78},{255,112,146},{155,133,255},{100,239,156}};
    node->m_icon->setColors(colors[(index-1)%5],colors[(index+1)%5]);
    return node;
}

PlayerIcon* PlayerIcon::createRemote(int accountId) {
    auto* result = new PlayerIcon();
    if (result && result->initRemote(accountId)) {
        result->autorelease();
        return result;
    }
    delete result;
    return nullptr;
}

PlayerIcon::~PlayerIcon() {
    releaseDelegate();
}

bool PlayerIcon::initFallback() {
    if (!CCNode::init()) return false;
    setContentSize(CCSizeZero);
    m_icon = SimplePlayer::create(1);
    if (!m_icon) return false;
    m_icon->setPosition(CCPointZero);
    addChild(m_icon);
    return true;
}

bool PlayerIcon::initLocal() {
    if (!initFallback()) return false;
    m_local = true;
    applyLocalCosmetics();
    return true;
}

bool PlayerIcon::initRemote(int accountId) {
    if (!initFallback()) return false;
    m_resolution = corum::ranked::PlayerIconResolution(accountId);
    return true;
}

void PlayerIcon::onEnter() {
    CCNode::onEnter();
    if (!m_local && m_resolution.hasRemoteIdentity()) {
        schedule(schedule_selector(PlayerIcon::resolveRemote), 0.25f);
        resolveRemote(0.0f);
    }
}

void PlayerIcon::onExit() {
    unschedule(schedule_selector(PlayerIcon::resolveRemote));
    releaseDelegate();
    CCNode::onExit();
}

void PlayerIcon::applyLocalCosmetics() {
    auto* game = GameManager::sharedState();
    if (!game || !m_icon) return;
    m_icon->updatePlayerFrame(std::max(1, game->getPlayerFrame()), IconType::Cube);
    m_icon->setColors(
        game->colorForIdx(game->getPlayerColor()),
        game->colorForIdx(game->getPlayerColor2())
    );
    if (game->getPlayerGlow()) {
        m_icon->setGlowOutline(game->colorForIdx(game->getPlayerGlowColor()));
    } else {
        m_icon->disableGlowOutline();
    }
}

void PlayerIcon::applyRemoteCosmetics(GJUserScore* score) {
    if (!score || !m_icon || !m_resolution.accept(score->m_accountID)) return;
    auto* game = GameManager::sharedState();
    if (!game) return;
    auto const frame = std::max(1, score->m_iconID > 0 ? score->m_iconID : score->m_playerCube);
    m_icon->updatePlayerFrame(frame, score->m_iconType);
    m_icon->setColors(game->colorForIdx(score->m_color1), game->colorForIdx(score->m_color2));
    if (score->m_glowEnabled) {
        m_icon->setGlowOutline(game->colorForIdx(score->m_color3));
    } else {
        m_icon->disableGlowOutline();
    }
    m_requestInFlight = false;
    unschedule(schedule_selector(PlayerIcon::resolveRemote));
    releaseDelegate();
}

void PlayerIcon::resolveRemote(float) {
    if (m_resolution.resolved() || !m_resolution.hasRemoteIdentity()) return;
    auto* manager = GameLevelManager::sharedState();
    if (!manager) return;
    if (auto* cached = manager->userInfoForAccountID(m_resolution.expectedAccountId())) {
        applyRemoteCosmetics(cached);
        return;
    }
    if (m_requestInFlight) return;
    if (manager->m_userInfoDelegate && manager->m_userInfoDelegate != this) return;
    manager->m_userInfoDelegate = this;
    m_requestInFlight = true;
    manager->getGJUserInfo(m_resolution.expectedAccountId());
}

void PlayerIcon::releaseDelegate() {
    auto* manager = GameLevelManager::sharedState();
    if (manager && manager->m_userInfoDelegate == this) manager->m_userInfoDelegate = nullptr;
    m_requestInFlight = false;
}

void PlayerIcon::getUserInfoFinished(GJUserScore* score) {
    if (!score || score->m_accountID != m_resolution.expectedAccountId()) {
        releaseDelegate();
        return;
    }
    applyRemoteCosmetics(score);
}

void PlayerIcon::getUserInfoFailed(int accountId) {
    if (accountId != m_resolution.expectedAccountId()) return;
    releaseDelegate();
    // Keep the fallback and retry later. This also lets a profile cached by a
    // different screen replace it without rebuilding the Ranked UI.
}

void PlayerIcon::userInfoChanged(GJUserScore* score) {
    if (!score || score->m_accountID != m_resolution.expectedAccountId()) return;
    applyRemoteCosmetics(score);
}

} // namespace corum::ranked::ui
