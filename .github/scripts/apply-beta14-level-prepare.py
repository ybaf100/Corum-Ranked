from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

path = Path("corum-ranked-mod/src/LevelInfoLayer.cpp")
text = path.read_text()

text = replace_once(
    text,
    '''bool rankedDownloadGateState(std::string const& state) {\n    return\n        rankedPrepareState(state) ||\n        state == "ROUND_PLAYING" ||\n        state == "DEATHMATCH_PLAYING" ||\n        state == "MAP_PLAYING";\n}\n''',
    '''bool rankedDownloadGateState(std::string const& state) {\n    return\n        rankedPrepareState(state) ||\n        state == "ROUND_PLAYING" ||\n        state == "DEATHMATCH_PLAYING" ||\n        state == "MAP_PLAYING";\n}\n\nbool jukeboxLoaded() {\n    return Loader::get() && Loader::get()->isModLoaded("fleym.nongd");\n}\n\nbool hasOpenModal(CCNode* root) {\n    if (!root) return false;\n    for (auto* child : root->getChildrenExt()) {\n        if (!child) continue;\n        if (typeinfo_cast<FLAlertLayer*>(child)) return true;\n        if (hasOpenModal(child)) return true;\n    }\n    return false;\n}\n''',
    "jukebox and modal helpers",
)

text = replace_once(
    text,
    '''void setMenuItemsEnabled(CCNode* root, bool enabled) {\n    if (!root) return;\n    if (auto* item = typeinfo_cast<CCMenuItem*>(root)) item->setEnabled(enabled);\n    for (CCNode* child : root->getChildrenExt()) {\n        if (child) setMenuItemsEnabled(child, enabled);\n    }\n}''',
    '''void setMenuItemsEnabled(CCNode* root, bool enabled) {\n    if (!root) return;\n    // The dedicated Level Options button stays usable during Ranked prepare so\n    // LDM / Disable Shake remain presentation choices, not anti-cheat locks.\n    if (!enabled && root->getID() == "ranked-prepare-settings-menu"_spr) {\n        if (auto* menu = typeinfo_cast<CCMenu*>(root)) menu->setTouchEnabled(true);\n        for (CCNode* child : root->getChildrenExt()) {\n            if (auto* item = typeinfo_cast<CCMenuItem*>(child)) item->setEnabled(true);\n        }\n        return;\n    }\n    if (auto* item = typeinfo_cast<CCMenuItem*>(root)) item->setEnabled(enabled);\n    for (CCNode* child : root->getChildrenExt()) {\n        if (child) setMenuItemsEnabled(child, enabled);\n    }\n}''',
    "preserve settings control",
)

text = replace_once(
    text,
    '''void enableSongAcquisitionControls(CustomSongWidget* songWidget) {\n    if (!songWidget) return;\n\n    // CustomSongWidget is a CCNode with its own button menu. Disabling parent\n    // CCMenu touch dispatch (the alpha.21 approach) can leave the visible song\n    // download button completely inert. Keep menu dispatch alive and gate at the\n    // individual menu-item level instead.\n    if (songWidget->m_buttonMenu) songWidget->m_buttonMenu->setTouchEnabled(true);\n\n    auto enable = [](CCMenuItemSpriteExtra* item) {''',
    '''void enableSongAcquisitionControls(CustomSongWidget* songWidget) {\n    if (!songWidget) return;\n\n    // Jukebox opens its NONG manager from the song widget itself. When it is\n    // actually loaded, restore the whole song-widget subtree rather than only\n    // vanilla download buttons. The effective file is still validated through\n    // MusicDownloadManager::pathForSong() before Ranked can launch.\n    if (jukeboxLoaded()) setMenuItemsEnabled(songWidget, true);\n\n    // CustomSongWidget is a CCNode with its own button menu. Disabling parent\n    // CCMenu touch dispatch (the alpha.21 approach) can leave the visible song\n    // download button completely inert. Keep menu dispatch alive and gate at the\n    // individual menu-item level instead.\n    if (songWidget->m_buttonMenu) songWidget->m_buttonMenu->setTouchEnabled(true);\n\n    auto enable = [](CCMenuItemSpriteExtra* item) {''',
    "enable Jukebox song widget",
)

text = replace_once(
    text,
    '''        bool mapFailureReported = false;\n        SteadyClock::time_point gateOpenedAt {};''',
    '''        bool mapFailureReported = false;\n        CCMenu* rankedSettingsMenu = nullptr;\n        SteadyClock::time_point gateOpenedAt {};''',
    "settings menu field",
)

text = replace_once(
    text,
    '''        m_fields->gateProgressLabel = gateLabel("30%", 0.17f, {183.0f, 12.0f}, corum::ranked::ui::Text);\n        m_fields->gateOverlay->addChild(m_fields->gateProgressLabel, 4);\n\n        auto const remainingAtOpen =''',
    '''        m_fields->gateProgressLabel = gateLabel("30%", 0.17f, {183.0f, 12.0f}, corum::ranked::ui::Text);\n        m_fields->gateOverlay->addChild(m_fields->gateProgressLabel, 4);\n\n        // Keep the real vanilla Level Options popup available throughout prepare.\n        // It contains LDM / Disable Shake and does not alter Ranked scoring.\n        m_fields->rankedSettingsMenu = CCMenu::create();\n        m_fields->rankedSettingsMenu->setID("ranked-prepare-settings-menu"_spr);\n        m_fields->rankedSettingsMenu->setPosition({size.width - 24.0f, size.height - 24.0f});\n        if (auto* optionsSprite = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png")) {\n            optionsSprite->setScale(0.62f);\n            auto* optionsItem = CCMenuItemSpriteExtra::create(\n                optionsSprite,\n                this,\n                menu_selector(CorumRankedLevelInfoLayer::onRankedLevelOptions)\n            );\n            optionsItem->setID("ranked-prepare-settings"_spr);\n            m_fields->rankedSettingsMenu->addChild(optionsItem);\n        }\n        addChild(m_fields->rankedSettingsMenu, 1005);\n\n        auto const remainingAtOpen =''',
    "create level options button",
)

text = replace_once(
    text,
    '''    void updateSongDownloadGate(float) {\n        if (!m_fields->songGateActive) return;''',
    '''    void onRankedLevelOptions(CCObject* sender) {\n        if (!m_fields->songGateActive || !isCurrentRankedLevel(m_level)) return;\n        // Invoke Geometry Dash's own options layer. Ranked does not synthesize or\n        // reinterpret LDM / Disable Shake values.\n        LevelInfoLayer::onLevelOptions(sender);\n    }\n\n    void updateSongDownloadGate(float) {\n        if (!m_fields->songGateActive) return;''',
    "options callback",
)

# Hold auto launch while any modal spawned by Level Options or Jukebox is open.
text = replace_once(
    text,
    '''                now < m_fields->nextGameplayLaunchAt ||\n                m_fields->songGateStartingPlay ||\n                m_fields->gameplayLaunchStarted\n            ) return;''',
    '''                now < m_fields->nextGameplayLaunchAt ||\n                m_fields->songGateStartingPlay ||\n                m_fields->gameplayLaunchStarted ||\n                hasOpenModal(CCDirector::sharedDirector()->getRunningScene())\n            ) return;''',
    "modal auto-play hold",
)

text = replace_once(
    text,
    '''        m_fields->gateIntroPanel = nullptr;\n        m_fields->gateTitle = nullptr;''',
    '''        if (m_fields->rankedSettingsMenu) {\n            m_fields->rankedSettingsMenu->removeFromParentAndCleanup(true);\n            m_fields->rankedSettingsMenu = nullptr;\n        }\n        m_fields->gateIntroPanel = nullptr;\n        m_fields->gateTitle = nullptr;''',
    "teardown settings menu",
)

path.write_text(text)
