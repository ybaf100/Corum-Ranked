import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const read = path => readFileSync(join(root, path), 'utf8');
const visuals = read('src/ui/RankedVisuals.hpp');
const fonts = JSON.parse(read('mod.json')).resources.fonts;
function method(source, signature) {
    const start = source.indexOf(signature);
    assert(start >= 0, signature);
    const open = source.indexOf('{', start);
    let end = open + 1, depth = 1;
    while (depth) {
        if (source[end] === '{') ++depth;
        if (source[end] === '}') --depth;
        ++end;
    }
    return source.slice(start, end);
}

test('source font generation keeps all weights and isolates large countdown digits', () => {
    for (const name of ['CorumSans', 'CorumSansMedium', 'CorumSansSemiBold', 'CorumSansBold', 'CorumSansBlack']) {
        assert.equal(fonts[name].size, 128, name);
        assert.equal(fonts[name].path, `resources/${name}.ttf`);
    }
    assert.deepEqual(fonts.CorumSansCountdown, {
        path: 'resources/CorumSansBlack.ttf', size: 256, charset: '32,48-57',
    });
    const overlay = read('src/ui/RankedEntryOverlay.cpp');
    assert.match(overlay, /FontWeight::Black, countdownFont\(\)/);
    assert.match(overlay, /setLabelScale\(m_digit, 1\.7f \* frame.scale\)/);
});

test('production label helpers preserve geometry, width floors, animation scales and native fallback', () => {
    const cpp = `
#include "${join(root, 'src/domain/FontMetrics.hpp')}"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <vector>
struct CCPoint {float x,y;};
struct ccColor3B {unsigned char r,g,b;};
struct CCLabelBMFont {
    static inline bool failCustom=false;
    static inline std::vector<std::unique_ptr<CCLabelBMFont>> labels;
    std::string text, resource;float scale=1, naturalWidth=240;ccColor3B color{};CCPoint position{};
    static CCLabelBMFont* create(char const* text,char const* resource) {
        if(failCustom&&std::string_view(resource).find("CorumSans")!=std::string_view::npos)return nullptr;
        auto p=std::make_unique<CCLabelBMFont>();p->text=text;p->resource=resource;
        if(p->resource.find("CorumSansCountdown")!=std::string::npos)p->naturalWidth*=4;
        else if(p->resource.find("CorumSans")!=std::string::npos)p->naturalWidth*=2;
        labels.push_back(std::move(p));return labels.back().get();
    }
    char const* getFntFile()const{return resource.c_str();}
    void setScale(float value){scale=value;}
    void setColor(ccColor3B value){color=value;}
    void setPosition(CCPoint value){position=value;}
    void limitLabelWidth(float width,float maximum,float minimum){
        scale=std::clamp(width/naturalWidth,minimum,maximum);
    }
    float displayedWidth()const{return naturalWidth*scale;}
};
namespace corum::ranked::ui {
enum class FontWeight {Regular,Medium,SemiBold,ExtraBold,Black};
char const* font(FontWeight){return "hwanhee1.corum_ranked/CorumSansBlack.fnt";}
${method(visuals, 'inline float fontScale(')}
${method(visuals, 'inline void setLabelScale(')}
${method(visuals, 'inline void fitLabelWidth(')}
${method(visuals, 'inline CCLabelBMFont* label(')}
}
bool same(float a,float b){return std::abs(a-b)<0.0001f;}
int main(){
    using namespace corum::ranked;using namespace corum::ranked::ui;
    auto* normal=label("한글 / English",0.4f,{21,32},{230,231,232},FontWeight::Black);
    assert(same(normal->scale,0.2f)&&same(normal->displayedWidth(),240*0.4f));
    assert(normal->text=="한글 / English"&&normal->position.x==21&&normal->color.r==230);
    fitLabelWidth(normal,80,0.5f,0.2f);
    assert(same(normal->displayedWidth(),80)&&same(normal->scale,80.0f/480));
    fitLabelWidth(normal,5,0.5f,0.2f);
    assert(same(normal->scale,0.1f)); // Logical readability floor preserved.
    fitLabelWidth(normal,1000,0.5f,0.2f);assert(same(normal->scale,0.25f));
    setLabelScale(normal,0.4f);assert(same(normal->displayedWidth(),96));
    auto* digit=label("3",1.7f,{0,0},{255,255,255},FontWeight::Black,"CorumSansCountdown.fnt");
    assert(same(digit->scale,1.7f/4)&&same(digit->displayedWidth(),240*1.7f));
    for(float animated:{1.32f,1.15f,1.0f,0.9f}){
        setLabelScale(digit,1.7f*animated);
        assert(same(digit->displayedWidth(),240*1.7f*animated));
    }
    for(unsigned factor:{1u,2u,4u}){
        auto const before=static_cast<float>(CorumFontReferenceSize)/factor;
        assert(same(before*0.4f,(static_cast<float>(CorumFontRasterSize)/factor)*corumFontScale(0.4f,"CorumSans-hd.fnt")));
        assert(same(before*1.7f,(static_cast<float>(CorumCountdownRasterSize)/factor)*corumFontScale(1.7f,"CorumSansCountdown-uhd.fnt")));
    }
    CCLabelBMFont::failCustom=true;
    auto* fallback=label("Fallback",0.4f,{0,0},{255,255,255},FontWeight::Black);
    assert(fallback->resource=="goldFont.fnt"&&same(fallback->scale,0.4f));
    fitLabelWidth(fallback,80,0.5f,0.2f);assert(same(fallback->displayedWidth(),80));
    auto* body=label("Fallback body",0.4f,{0,0},{255,255,255},FontWeight::Regular);
    assert(body->resource=="bigFont.fnt"&&same(body->scale,0.4f));
    setLabelScale(nullptr,1);fitLabelWidth(nullptr,100,1,0.1f);assert(same(fontScale(nullptr,1),1));
}
`;
    const temp = mkdtempSync(join(tmpdir(), 'corum-font-native-'));
    try {
        const source = join(temp, 'font.cpp'), binary = join(temp, 'font');
        writeFileSync(source, cpp);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', source, '-o', binary]);
        execFileSync(binary);
    } finally { rmSync(temp, {recursive:true, force:true}); }
});

test('Corum width clamps cannot restore an unnormalized scale in any UI screen', () => {
    for (const path of ['src/RankedPopup.cpp', 'src/LevelInfoLayer.cpp', 'src/PlayLayer.cpp',
        'src/ui/RankedSurrenderPopup.hpp', 'src/ui/RankTransitionLayer.cpp',
        'src/ui/RankedEntryOverlay.cpp', 'src/debug/DebugSpectatePopup.cpp']) {
        assert.doesNotMatch(read(path), /->limitLabelWidth\(/, path);
    }
});
