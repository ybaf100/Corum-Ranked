#include "AchievementEffects.hpp"
#include <Geode/cocos/shaders/CCGLProgram.h>
#include <Geode/cocos/shaders/CCShaderCache.h>
#include <chrono>
using namespace geode::prelude;
namespace corum::ranked::ui {namespace {
constexpr int EffectTag=0x41435632;
constexpr char Vertex[]=R"GLSL(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
#ifdef GL_ES
varying mediump vec2 v_uv;
varying mediump float v_x;
varying lowp vec4 v_color;
#else
varying vec2 v_uv;
varying float v_x;
varying vec4 v_color;
#endif
void main(){gl_Position=CC_MVPMatrix*a_position;v_uv=a_texCoord;v_x=a_position.x;v_color=a_color;}
)GLSL";
constexpr char Fragment[]=R"GLSL(
#ifdef GL_ES
precision mediump float;
varying mediump vec2 v_uv;
varying mediump float v_x;
varying lowp vec4 v_color;
#else
varying vec2 v_uv;
varying float v_x;
varying vec4 v_color;
#endif
uniform sampler2D CC_Texture0;
uniform float u_width;
uniform float u_phase;
uniform float u_mythic;
uniform float u_solid;
vec3 pastel(float p){
 float s=fract(p)*7.;
 vec3 a=vec3(255.,184.,198.);vec3 b=vec3(255.,213.,176.);
 if(s>=6.){a=vec3(243.,196.,235.);b=vec3(255.,184.,198.);}
 else if(s>=5.){a=vec3(211.,194.,255.);b=vec3(243.,196.,235.);}
 else if(s>=4.){a=vec3(185.,223.,255.);b=vec3(211.,194.,255.);}
 else if(s>=3.){a=vec3(189.,240.,209.);b=vec3(185.,223.,255.);}
 else if(s>=2.){a=vec3(255.,242.,179.);b=vec3(189.,240.,209.);}
 else if(s>=1.){a=vec3(255.,213.,176.);b=vec3(255.,242.,179.);}
 return mix(a,b,fract(s))/255.;
}
void main(){
 float x=v_x/max(u_width,1.);
 // The narrow highlight is outside both edges at the loop boundary.
 float band=1.-smoothstep(.015,.115,abs(x-(-.2+1.4*u_phase)));
 vec3 gold=mix(vec3(255.,176.,70.)/255.,vec3(255.,244.,210.)/255.,band*.82);
 vec3 color=u_mythic>.5?pastel(x+u_phase):gold;
 vec4 texel=u_solid>.5?vec4(1.):texture2D(CC_Texture0,v_uv);
 gl_FragColor=texel*v_color*vec4(color,1.);
}
)GLSL";
CCGLProgram* makeProgram(){
 auto* p=new CCGLProgram();
 if(!p->initWithVertexShaderByteArray(Vertex,Fragment)){delete p;return nullptr;}
 p->addAttribute(kCCAttributeNamePosition,kCCVertexAttrib_Position);p->addAttribute(kCCAttributeNameColor,kCCVertexAttrib_Color);p->addAttribute(kCCAttributeNameTexCoord,kCCVertexAttrib_TexCoords);
 if(!p->link()){delete p;return nullptr;}p->updateUniforms();p->autorelease();return p;
}
class AchievementFlow final:public CCActionInterval{
 Ref<CCGLProgram> m_program;int m_tier=0;bool m_solid=false;GLint m_width=-1,m_phase=-1;
public:
 static AchievementFlow* create(CCGLProgram* p,int tier,bool solid){
  auto* a=new AchievementFlow();if(!a->initWithDuration(solid||tier==5?3.f:2.f)){delete a;return nullptr;}
  a->m_program=p;a->m_tier=tier;a->m_solid=solid;
  if(p){a->m_width=p->getUniformLocationForName("u_width");a->m_phase=p->getUniformLocationForName("u_phase");}
  a->autorelease();return a;
 }
 void update(float) override{
  auto* target=getTarget();if(!target)return;
  auto const seconds=std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
  auto const period=m_solid||m_tier==5?3.:2.;auto const phase=static_cast<float>(std::fmod(seconds,period)/period);
  if(m_program){m_program->use();m_program->setUniformLocationWith1f(m_width,std::max(1.f,target->getContentSize().width));m_program->setUniformLocationWith1f(m_phase,phase);}
  else if(auto* label=typeinfo_cast<CCLabelBMFont*>(target)){
   auto const c=m_tier==5?achievementPastelAt(0,seconds):achievementTierColor(m_tier);
   label->setColor({static_cast<GLubyte>(c.r),static_cast<GLubyte>(c.g),static_cast<GLubyte>(c.b)});
  }else if(m_solid){auto* fill=static_cast<CCLayerColor*>(target);auto const c=m_tier==5?achievementPastelAt(0,seconds):achievementTierColor(m_tier);fill->setColor({static_cast<GLubyte>(c.r),static_cast<GLubyte>(c.g),static_cast<GLubyte>(c.b)});}
 }
};
void attach(CCNode* node,int tier,bool solid){
 auto* p=makeProgram();if(p){
  node->setShaderProgram(p);p->use();p->setUniformLocationWith1f(p->getUniformLocationForName("u_mythic"),tier==5?1.f:0.f);
  p->setUniformLocationWith1f(p->getUniformLocationForName("u_solid"),solid?1.f:0.f);
 }else {static bool logged=false;if(!logged){logged=true;log::warn("[AchievementEffects] shader unavailable; using smooth text-color fallback");}}
 auto* flow=AchievementFlow::create(p,tier,solid);if(!flow)return;flow->update(0);
 auto* action=CCRepeatForever::create(flow);action->setTag(EffectTag);node->runAction(action);
}
}
ccColor3B achievementColor(int tier){auto const c=achievementTierColor(tier);return {static_cast<GLubyte>(c.r),static_cast<GLubyte>(c.g),static_cast<GLubyte>(c.b)};}
void styleAchievementText(CCLabelBMFont* text,int tier,bool animated){
 if(!text)return;text->stopActionByTag(EffectTag);
 text->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionTextureColor));
 text->setColor(achievementColor(tier));if(animated && tier>=4 && tier<=5){text->setColor({255,255,255});attach(text,tier,false);}
}
void styleAchievementTree(CCNode* node,int tier){
 if(!node)return;if(auto* label=typeinfo_cast<CCLabelBMFont*>(node)){styleAchievementText(label,tier,false);return;}
 for(auto* child:CCArrayExt<CCNode*>(node->getChildren()))styleAchievementTree(child,tier);
}
void styleAchievementFill(CCLayerColor* fill,int tier,bool rainbowProgress,bool achieved){
 if(!fill)return;
 fill->stopActionByTag(EffectTag);
 fill->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionColor));
 fill->setColor(achievementColor(tier));
 // Preserve the existing animated seven-pastel rainbow only for V/Mythic progress bars.
 // Achieved IV/Legendary progress gets the same gold sweep as its title.
 // Claim All/buttons and all lower-stage bars keep static tier colors.
 if(rainbowProgress && tier==5){fill->setColor({255,255,255});attach(fill,tier,true);}
 if(rainbowProgress && tier==4 && achieved){fill->setColor({255,255,255});attach(fill,tier,true);}
}
}
