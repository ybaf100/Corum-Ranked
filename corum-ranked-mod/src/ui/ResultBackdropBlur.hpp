#pragma once

#include <Geode/Geode.hpp>
#include <Geode/cocos/misc_nodes/CCRenderTexture.h>
#include <Geode/cocos/shaders/CCGLProgram.h>

namespace corum::ranked::ui {

// Captured once at result entry, not a per-frame screen readback or CPU blur.
cocos2d::CCSprite* captureBlurredResult(cocos2d::CCNode* source, cocos2d::CCSize size);

} // namespace corum::ranked::ui
