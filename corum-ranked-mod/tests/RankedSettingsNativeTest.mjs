import assert from 'node:assert/strict';
import test from 'node:test';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('..',import.meta.url)));
const read=p=>readFileSync(join(root,p),'utf8');
const body=(source,name)=>{const at=source.indexOf(name);assert(at>=0,name);const open=source.indexOf('{',at);let end=open+1,depth=1;while(depth){if(source[end]==='{')++depth;if(source[end]==='}')--depth;++end;}return source.slice(at,end);};

test('server-clock cooldown disables and grays refresh, shows remaining time and re-enables exactly at expiry',()=>{
    const cpp=`
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "${join(root,'src/domain/ServerClock.hpp')}"
struct CCObject {};
struct CCNode {virtual ~CCNode()=default;bool gray=false;};
struct CCLabelBMFont:CCNode {std::string text;void setString(char const* v){text=v;}};
struct Button {bool enabled=true;CCNode* face=nullptr;void setEnabled(bool v){enabled=v;}void setNormalImage(CCNode* v){face=v;}};
namespace fmt {
std::string format(char const*,std::string const& name,std::int64_t minutes,std::int64_t seconds){return name+" "+(minutes<10?"0":"")+std::to_string(minutes)+":"+(seconds<10?"0":"")+std::to_string(seconds);}
std::string format(char const*,int rank){return "#"+std::to_string(rank);}
}
namespace corum::ranked {
std::int64_t localNow=0;std::int64_t localNowMillis(){return localNow;}
struct RankedRuntime {
 struct Record{std::string provider,status;std::optional<int> rank;};
 struct Forum {bool loading=false;std::string refreshAvailableAt,error;std::vector<Record> records;};
 struct View{Forum forumRecords;} m_view;ServerClock m_serverClock;
 static RankedRuntime& get(){static RankedRuntime r;return r;}
 View const& view()const{return m_view;}
 std::int64_t forumRefreshRemainingMs()const;
};
${body(read('src/RankedRuntime.cpp'),'std::int64_t RankedRuntime::forumRefreshRemainingMs()')}
namespace ui {
namespace i18n {std::string text(char const*,char const* fallback){return fallback;}}
void fitMenuLabelWidth(CCLabelBMFont*,float,float,float){}
void fitFullMenuLabel(CCLabelBMFont*,float,float,float){}
struct RankedSettingsPopup {
 bool m_closed=false,m_refreshDisabled=false;Button button;Button* m_refreshRecords=&button;
 CCLabelBMFont timer,status;CCLabelBMFont* m_forumCooldown=&timer;CCLabelBMFont* m_forumStatus=&status;
 CCNode grayFace,normalFace;int rebuilds=0;
 CCNode* refreshButtonFace(bool disabled){++rebuilds;grayFace.gray=true;return disabled?&grayFace:&normalFace;}
 ${body(read('src/ui/RankedSettingsPopup.hpp'),'void updateForumStatus(float)')}
};
}
}
int main(){using namespace corum::ranked;auto& r=RankedRuntime::get();
 localNow=*parseIso8601Millis("2026-10-09T00:00:00Z");
 r.m_serverClock.observe("2026-10-08T00:00:00Z",localNow); // client's clock is one day ahead
 r.m_view.forumRecords.records={{"GDKF","RANKED",18},{"POINTERCRATE","RANKED",241}};
 ui::RankedSettingsPopup p;p.updateForumStatus(0);assert(p.button.enabled&&p.timer.text.empty());
 r.m_view.forumRecords.refreshAvailableAt="2026-10-08T01:00:00Z";
 p.updateForumStatus(0);assert(!p.button.enabled&&p.button.face->gray&&p.timer.text=="Next refresh in 60:00");
 assert(p.status.text=="GDKF #18  /  Pointercrate #241"&&p.rebuilds==1);
 localNow+=500;p.updateForumStatus(0);assert(p.timer.text=="Next refresh in 60:00"&&p.rebuilds==1);
 localNow+=500;p.updateForumStatus(0);assert(p.timer.text=="Next refresh in 59:59"&&p.rebuilds==1);
 ui::RankedSettingsPopup reopened;reopened.updateForumStatus(0);
 assert(!reopened.button.enabled&&reopened.timer.text==p.timer.text); // reopening cannot reset the cooldown
 localNow+=3598999;p.updateForumStatus(0);assert(!p.button.enabled&&p.timer.text=="Next refresh in 00:01");
 localNow+=1;p.updateForumStatus(0);assert(p.button.enabled&&!p.button.face->gray&&p.timer.text.empty()&&p.rebuilds==2);
 r.m_view.forumRecords.loading=true;p.updateForumStatus(0);assert(!p.button.enabled&&p.button.face->gray);
 r.m_view.forumRecords.loading=false;p.updateForumStatus(0);assert(p.button.enabled);
 p.m_closed=true;r.m_view.forumRecords.loading=true;p.updateForumStatus(0);assert(p.button.enabled);
 r.m_view.forumRecords.refreshAvailableAt.clear();assert(r.forumRefreshRemainingMs()==0);
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-settings-native-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),join(root,'src/domain/ServerClock.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});

test('custom button gray treatment reaches independent border, fill and label nodes without losing their contrast',()=>{
    const cpp=`
#include <cassert>
#include <vector>
using GLubyte=unsigned char;
struct ccColor3B {GLubyte r,g,b;};ccColor3B ccc3(GLubyte r,GLubyte g,GLubyte b){return {r,g,b};}
struct CCNode {virtual ~CCNode()=default;std::vector<CCNode*> children;auto getChildrenExt(){return children;}};
struct CCSprite:CCNode {ccColor3B color{64,224,255};int opacity=250;void setColor(ccColor3B v){color=v;}ccColor3B getColor()const{return color;}};
struct CCLabelBMFont:CCSprite {};
struct CCLayerColor:CCSprite {};
template<class T>auto typeinfo_cast(CCNode* n){return dynamic_cast<T>(n);}
${body(read('src/ui/RankedVisuals.hpp'),'inline void grayButtonVisual(')}
int main(){CCNode menu,face;CCLayerColor border,fill;fill.color={15,28,50};CCLabelBMFont label;CCSprite icon;
 menu.children={&face};face.children={&border,&fill,&label,&icon};grayButtonVisual(&menu);
 assert(border.color.r==border.color.g&&border.color.g==border.color.b);
 assert(fill.color.r==fill.color.g&&fill.color.g==fill.color.b&&border.color.r>fill.color.r);
 assert(label.color.r==176&&label.color.g==176&&label.color.b==176&&label.color.r>fill.color.r);
 assert(icon.color.r==icon.color.g&&icon.color.g==icon.color.b&&icon.opacity==250);
 grayButtonVisual(nullptr);
}
`;
    const dir=mkdtempSync(join(tmpdir(),'corum-gray-native-'));
    try{writeFileSync(join(dir,'test.cpp'),cpp);execFileSync('g++',['-std=c++23','-Wall','-Wextra','-Wpedantic','-Werror',join(dir,'test.cpp'),'-o',join(dir,'test')],{stdio:'pipe'});execFileSync(join(dir,'test'),[],{stdio:'pipe'});}finally{rmSync(dir,{recursive:true,force:true});}
});
