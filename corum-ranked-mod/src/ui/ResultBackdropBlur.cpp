#include "ResultBackdropBlur.hpp"

#include <cmath>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {
constexpr char kVertex[] = R"GLSL(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
uniform mat4 CC_MVPMatrix;
#ifdef GL_ES
varying mediump vec2 v_uv;
varying lowp vec4 v_color;
#else
varying vec2 v_uv;
varying vec4 v_color;
#endif
void main() { gl_Position = CC_MVPMatrix * a_position; v_uv = a_texCoord; v_color = a_color; }
)GLSL";
constexpr char kFragment[] = R"GLSL(
#ifdef GL_ES
precision mediump float;
#endif
#ifdef GL_ES
varying mediump vec2 v_uv;
varying lowp vec4 v_color;
#else
varying vec2 v_uv;
varying vec4 v_color;
#endif
uniform sampler2D CC_Texture0;
uniform vec2 u_step;
void main() {
    vec4 c = texture2D(CC_Texture0, v_uv) * 0.2270270270;
    c += texture2D(CC_Texture0, v_uv + u_step * 1.3846153846) * 0.3162162162;
    c += texture2D(CC_Texture0, v_uv - u_step * 1.3846153846) * 0.3162162162;
    c += texture2D(CC_Texture0, v_uv + u_step * 3.2307692308) * 0.0702702703;
    c += texture2D(CC_Texture0, v_uv - u_step * 3.2307692308) * 0.0702702703;
    gl_FragColor = c * v_color;
}
)GLSL";

CCGLProgram* blurProgram(float x, float y) {
    auto* program = new CCGLProgram();
    if (!program->initWithVertexShaderByteArray(kVertex, kFragment)) { delete program; return nullptr; }
    program->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    program->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    program->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    if (!program->link()) { delete program; return nullptr; }
    program->updateUniforms(); program->use();
    program->setUniformLocationWith2f(program->getUniformLocationForName("u_step"), x, y);
    program->autorelease(); return program;
}

CCSprite* renderSprite(CCRenderTexture* texture) {
    auto* sprite = CCSprite::createWithTexture(texture->getSprite()->getTexture(), texture->getSprite()->getTextureRect());
    if (sprite) sprite->setFlipY(texture->getSprite()->isFlipY());
    return sprite;
}
} // namespace

CCSprite* captureBlurredResult(CCNode* source, CCSize size) {
    if (!source || size.width <= 0 || size.height <= 0) return nullptr;
    auto* capture = CCRenderTexture::create(static_cast<int>(std::ceil(size.width)), static_cast<int>(std::ceil(size.height)));
    if (!capture) return nullptr;
    capture->beginWithClear(0, 0, 0, 1); source->visit(); capture->end();
    // Windows rpcndr.h defines `small` as a macro; keep this identifier distinct.
    auto const blurSize = CCSize {std::ceil(size.width / 2), std::ceil(size.height / 2)};
    auto* horizontal = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));
    auto* vertical = CCRenderTexture::create(static_cast<int>(blurSize.width), static_cast<int>(blurSize.height));
    if (!horizontal || !vertical) return nullptr;
    auto* first = renderSprite(capture);
    auto* hProgram = blurProgram(6.0f / capture->getSprite()->getTexture()->getPixelsWide(), 0);
    if (!first || !hProgram) return nullptr;
    first->setShaderProgram(hProgram);
    first->setScaleX(blurSize.width / size.width); first->setScaleY(blurSize.height / size.height);
    first->setPosition({blurSize.width / 2, blurSize.height / 2});
    horizontal->beginWithClear(0,0,0,1); first->visit(); horizontal->end();
    auto* second = renderSprite(horizontal);
    auto* vProgram = blurProgram(0, 3.0f / horizontal->getSprite()->getTexture()->getPixelsHigh());
    if (!second || !vProgram) return nullptr;
    second->setShaderProgram(vProgram); second->setPosition({blurSize.width / 2, blurSize.height / 2});
    vertical->beginWithClear(0,0,0,1); second->visit(); vertical->end();
    auto* result = renderSprite(vertical);
    if (!result) return nullptr;
    result->setScaleX(size.width / blurSize.width); result->setScaleY(size.height / blurSize.height);
    result->setPosition({size.width / 2, size.height / 2});
    return result;
}

} // namespace corum::ranked::ui
