// Compile production shared components with Cocos API doubles, not a device renderer.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,writeFileSync,mkdtempSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=path=>readFileSync(join(root,path),'utf8');
function method(source,name){const start=source.indexOf(name);assert(start>=0,name);let end=source.indexOf('{',start)+1,depth=1;while(depth){depth+=(source[end]==='{')-(source[end]==='}');end++;}return source.slice(start,end);}
function run(cpp){const dir=mkdtempSync(join(tmpdir(),'corum-survival-ui-'));try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror','-I',join(root,'src'),join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}catch(e){throw new Error(e.stderr?.toString()||e.message);}finally{rmSync(dir,{recursive:true,force:true});}}

test('Solo and Survival shared HUD fonts retain native fallback, shadows, width and opacity',()=>{
    const visuals=read('src/ui/RankedVisuals.hpp');
    run(`
#include "domain/FontMetrics.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
using GLubyte=unsigned char;
namespace cocos2d {
struct CCPoint{float x,y;};struct ccColor3B{unsigned char r,g,b;};
struct CCNode{virtual ~CCNode()=default;std::vector<CCNode*> children;void addChild(CCNode* n,int){children.push_back(n);}};
struct CCLabelBMFont:CCNode {
 std::string value,resource;float scale=1;CCPoint position{},anchor{};ccColor3B color{};GLubyte alpha=255;inline static bool fallback=false;
 static CCLabelBMFont* create(char const* t,char const* f){if(fallback&&std::string(f).find("CorumSans")!=std::string::npos)return nullptr;auto* n=new CCLabelBMFont;n->value=t;n->resource=f;return n;}
 char const* getFntFile()const{return resource.c_str();}float getScale()const{return scale;}
 void setString(char const* s){value=s;}void setScale(float s){scale=s;}void setColor(ccColor3B c){color=c;}void setPosition(CCPoint p){position=p;}void setAnchorPoint(CCPoint p){anchor=p;}void setOpacity(GLubyte a){alpha=a;}
 void limitLabelWidth(float w,float maximum,float minimum){scale=std::clamp(w/(resource.find("CorumSans")!=std::string::npos?480.f:240.f),minimum,maximum);}
};
}
using namespace cocos2d;
constexpr char const* operator""_spr(char const* s,std::size_t){return s;}
namespace corum::ranked::ui {
enum class FontWeight{Regular,Medium,SemiBold,ExtraBold,Black};
${['inline char const* font(','inline float fontScale(','inline void setLabelScale(','inline void fitLabelWidth(','inline CCLabelBMFont* label('].map(n=>method(visuals,n)).join('\n')}
}
${read('src/ui/RankedHudLabel.hpp').replace(/^#pragma once\s*|^#include .*$/gm,'')}
int main(){using namespace corum::ranked::ui;
 CCNode root;auto h=rankedHudLabel(&root,"03:00",FontWeight::Black,1.02f,{.5f,1},{250,300},{255,255,255});
 assert(root.children.size()==2&&h.foreground->resource=="CorumSansBlack.fnt");
 assert(std::abs(h.foreground->scale-.51f)<.001f&&h.shadow->scale==h.foreground->scale);
 assert(h.shadow->alpha==205&&h.shadow->position.x==250.75f&&h.shadow->position.y==299.25f);
 h.text("02:59");assert(h.foreground->value=="02:59"&&h.shadow->value==h.foreground->value);
 h.fit(70,.36f,.20f);assert(h.foreground->scale==h.shadow->scale);h.opacity(128);assert(h.foreground->alpha==128&&h.shadow->alpha==102);
 CCLabelBMFont::fallback=true;auto fallback=rankedHudLabel(&root,"한글",FontWeight::Black,.36f,{0,1},{8,270},{255,255,255});
 assert(fallback.foreground->resource=="goldFont.fnt"&&fallback.foreground->scale==.36f);
 for(auto* child:root.children)delete child;
}`);
});

test('chooser tap shine follows the same clockwise perimeter once and continues after hover input closes',()=>{
    const source=read('src/ui/LobbyHover.hpp');
    const shine=source.slice(source.indexOf('inline CCPoint lobbyCardPerimeter('),source.indexOf('// Owned by this lobby render'));
    run(`
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>
struct CCPoint{float x,y;};struct Color{float r,g,b,a;};
struct CCAction;struct CCNode{virtual ~CCNode()=default;bool removed=false;std::vector<CCNode*> children;CCAction* action=nullptr;void addChild(CCNode* c,int){children.push_back(c);}void runAction(CCAction* a){action=a;}void removeFromParentAndCleanup(bool){removed=true;}};
struct CCAction{virtual ~CCAction()=default;virtual void autorelease(){}virtual void startWithTarget(CCNode*){}virtual void update(float){}};
struct CCActionInterval:CCAction{float duration=0;bool initWithDuration(float d){duration=d;return true;}};
struct CCDrawNode:CCNode{std::vector<CCPoint> segments;static CCDrawNode* create(){return new CCDrawNode;}void clear(){segments.clear();}void drawSegment(CCPoint a,CCPoint,float,Color){segments.push_back(a);}};
struct CallFuncExt:CCAction{std::function<void()> fn;static CallFuncExt* create(std::function<void()> fn){auto* a=new CallFuncExt;a->fn=std::move(fn);return a;}void finish(){fn();}};
struct CCSequence:CCAction{CCAction* animation;CallFuncExt* done;static CCSequence* create(CCAction* a,CallFuncExt* d,std::nullptr_t){auto* s=new CCSequence;s->animation=a;s->done=d;return s;}};
struct CCMenuItemSpriteExtra{};
template<class T>struct Ref{T* p;T* data()const{return p;}T* operator->()const{return p;}explicit operator bool()const{return p;}};
template<class T>struct WeakRef{T* p;explicit WeakRef(T* p):p(p){}Ref<T> lock()const{return {p};}};
${shine}
struct Controller {
 struct Target{WeakRef<CCMenuItemSpriteExtra> item;WeakRef<CCNode> node;bool card=true,inside=false;CCDrawNode* shine=nullptr;float shineTime=1;};
 bool m_active=true,m_cardsSelectable=true;std::vector<Target> m_targets;int pulses=0;void pulse(Target&,bool){++pulses;}
 ${method(source,'void press(CCMenuItemSpriteExtra* item)')}
};
int main(){
 auto top=lobbyCardPerimeter(0),right=lobbyCardPerimeter(188),bottom=lobbyCardPerimeter(398),left=lobbyCardPerimeter(586);
 assert(top.x==-94&&top.y==105&&right.x==94&&right.y==105&&bottom.x==94&&bottom.y==-105&&left.x==-94&&left.y==-105);
 CCNode card;CCMenuItemSpriteExtra item;Controller hover;hover.m_targets.push_back({WeakRef(&item),WeakRef(&card)});
 hover.press(&item);assert(card.children.size()==1&&hover.pulses==1);
 auto* shine=dynamic_cast<CCDrawNode*>(card.children[0]);auto* seq=dynamic_cast<CCSequence*>(shine->action);assert(shine&&seq);
 hover.m_active=false;seq->animation->startWithTarget(shine);seq->animation->update(.5f);assert(shine->segments.size()==22);
 seq->animation->update(1);assert(shine->segments.empty());seq->done->finish();assert(shine->removed);
 hover.press(&item);assert(card.children.size()==1);hover.m_active=true;hover.m_cardsSelectable=false;hover.press(&item);assert(card.children.size()==1);
 delete seq->animation;delete seq->done;delete seq;delete shine;
}`);
});
