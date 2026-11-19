import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync, readdirSync} from 'node:fs';
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
        // Small text draws from 24/48 px atlases of the same weight and glyphs.
        for (const size of [24, 48])
            assert.deepEqual(fonts[name + size], {path: fonts[name].path, size, charset: fonts[name].charset}, name + size);
    }
    assert.deepEqual(fonts.CorumSansCountdown, {
        path: 'resources/CorumSansBlack.ttf', size: 256, charset: '32,48-57',
    });
    const overlay = read('src/ui/RankedEntryOverlay.cpp');
    assert.match(overlay, /FontWeight::Black, countdownFont\(\)/);
    assert.match(overlay, /setLabelScale\(m_digit, 1\.7f \* frame.scale\)/);
});

test('reserved Regular atlas avoids unused Korean duplicates while bilingual UI keeps its active weights',()=>{
    function sources(directory){return readdirSync(directory,{withFileTypes:true}).flatMap(entry=>entry.isDirectory()?sources(join(directory,entry.name)):[join(directory,entry.name)]);}
    for(const file of sources(join(root,'src')).filter(name=>/\.(?:hpp|cpp)$/.test(name)&&!name.endsWith('RankedVisuals.hpp'))){
        const source=readFileSync(file,'utf8');
        assert.doesNotMatch(source,/FontWeight::Regular|CorumSans\.fnt/,`Regular is reserved; add caption glyphs and validate the atlas budget before using it: ${file}`);
    }
    const charset=spec=>new Set(spec.split(',').flatMap(part=>{const[a,b=a]=part.split('-').map(Number);return Array.from({length:b-a+1},(_,i)=>a+i);}));
    assert.equal([...charset(fonts.CorumSans.charset)].some(n=>n>=0xac00&&n<=0xd7a3),false);
    for(const weight of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
        const glyphs=charset(fonts[weight].charset);
        for(const letter of '승급 승격 강등 기록 서바이벌 솔로 랭크 봇 수 설정 항복')assert(glyphs.has(letter.codePointAt(0)), `${weight} missing ${letter}`);
    }
    // The packaged SD/HD/UHD atlases of the four Hangul weights (128 px plus
    // the 24/48 px ones) share one 128 MiB RGBA budget (verify-font-package.py).
    // 352 Hangul glyphs fit the 128 px atlas; beta.149's 360 did not. Reword
    // with existing syllables instead of growing the set.
    for(const weight of ['CorumSansMedium','CorumSansSemiBold','CorumSansBold','CorumSansBlack']){
        const hangul=[...charset(fonts[weight].charset)].filter(n=>n>=0xac00&&n<=0xd7a3).length;
        assert(hangul<=352,`${weight} has ${hangul} Hangul glyphs; the font atlas budget allows 352`);
    }
});

test('production label helpers preserve geometry, width floors, animation scales and native fallback', () => {
    const cpp = `
#include "${join(root, 'src/domain/FontMetrics.hpp')}"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
struct CCPoint {float x,y;};
struct ccColor3B {unsigned char r,g,b;};
struct CCLabelBMFont {
    static inline bool failCustom=false;
    static inline std::string missing;
    static inline std::vector<std::unique_ptr<CCLabelBMFont>> labels;
    std::string text, resource;int z=0;float scale=1, naturalWidth=240;ccColor3B color{};CCPoint position{};
    static CCLabelBMFont* create(char const* text,char const* resource) {
        if(failCustom&&std::string_view(resource).find("CorumSans")!=std::string_view::npos)return nullptr;
        if(resource==missing)return nullptr;
        auto p=std::make_unique<CCLabelBMFont>();p->text=text;p->setFntFile(resource);
        labels.push_back(std::move(p));return labels.back().get();
    }
    // Glyph advances follow the atlas's glyph size.
    void setFntFile(char const* file){
        resource=file;naturalWidth=resource.find("CorumSans")==std::string::npos?240.0f:
            240.0f*static_cast<float>(corum::ranked::corumRasterSize(resource))/64.0f;
    }
    char const* getFntFile()const{return resource.c_str();}
    char const* getString()const{return text.c_str();}
    void setString(char const* value){text=value;}
    void setScale(float value){scale=value;}
    void setZOrder(int value){z=value;}
    float getScale()const{return scale;}
    struct Size{float width,height;};Size getContentSize()const{return {naturalWidth,32};}
    void setColor(ccColor3B value){color=value;}
    void setPosition(CCPoint value){position=value;}
    void limitLabelWidth(float width,float maximum,float minimum){
        scale=std::clamp(width/naturalWidth,minimum,maximum);
    }
    float displayedWidth()const{return naturalWidth*scale;}
};
namespace corum::ranked::ui {
enum class FontWeight {Regular,Medium,SemiBold,ExtraBold,Black};
// A window this large draws every label from the 128 px atlas, until main()
// switches it to 1920x1080 (3.375 screen pixels per point).
float pixelsPerPoint=100.0f;
float corumPixelsPerPoint(){return pixelsPerPoint;}
float corumContentScale(){return 4.0f;}
char const* font(FontWeight){return "hwanhee1.corum_ranked/CorumSansBlack.fnt";}
char const* font(FontWeight,float scale){
    switch(corum::ranked::corumFontTier(scale,corumPixelsPerPoint(),corumContentScale())){
        case 24:return "hwanhee1.corum_ranked/CorumSansBlack24.fnt";
        case 48:return "hwanhee1.corum_ranked/CorumSansBlack48.fnt";
        default:return font(FontWeight::Black);
    }
}
${method(visuals, 'inline float fontScale(')}
${method(visuals, 'inline bool corumFontLoads(')}
${method(visuals, 'inline void matchCorumFontTier(')}
${method(visuals, 'inline void setLabelScale(')}
${method(visuals, 'inline void fitLabelWidth(')}
${method(visuals, 'inline CCLabelBMFont* label(')}
${method(visuals, 'inline CCLabelBMFont* menuLabel(')}
${method(visuals, 'inline void fitMenuLabelWidth(')}
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
    auto* menu=menuLabel("Menu",0.25f,{0,0},{255,255,255},FontWeight::Black);
    assert(menu->scale>corumFontScale(0.25f,"CorumSansBlack.fnt")&&menu->z>3);
    // Readability floors cannot overflow a narrow translated button.
    fitMenuLabelWidth(menu,5,0.4f,0.3f);assert(menu->displayedWidth()<=5.001f);
    auto* hud=label("HUD",0.25f,{0,0},{255,255,255},FontWeight::Black);
    assert(same(hud->scale,corumFontScale(0.25f,"CorumSansBlack.fnt"))&&hud->z==0);
    CCLabelBMFont::failCustom=true;
    auto* fallback=label("Fallback",0.4f,{0,0},{255,255,255},FontWeight::Black);
    assert(fallback->resource=="goldFont.fnt"&&same(fallback->scale,0.4f));
    fitLabelWidth(fallback,80,0.5f,0.2f);assert(same(fallback->displayedWidth(),80));
    auto* body=label("Fallback body",0.4f,{0,0},{255,255,255},FontWeight::Regular);
    assert(body->resource=="bigFont.fnt"&&same(body->scale,0.4f));
    setLabelScale(nullptr,1);fitLabelWidth(nullptr,100,1,0.1f);assert(same(fontScale(nullptr,1),1));
    // 1920x1080: small text takes the small atlas and keeps its drawn size.
    CCLabelBMFont::failCustom=false;pixelsPerPoint=3.375f;
    auto* small=label("작은 글씨",0.36f,{0,0},{255,255,255},FontWeight::Black);
    assert(small->resource=="hwanhee1.corum_ranked/CorumSansBlack24.fnt"&&same(small->displayedWidth(),240*0.36f));
    // Shown larger later, it moves up (never enlarged from 24 px glyphs), and back.
    setLabelScale(small,0.9f);assert(small->resource=="hwanhee1.corum_ranked/CorumSansBlack48.fnt"&&same(small->displayedWidth(),240*0.9f));
    setLabelScale(small,1.5f);assert(small->resource=="hwanhee1.corum_ranked/CorumSansBlack.fnt"&&same(small->displayedWidth(),240*1.5f));
    setLabelScale(small,0.3f);assert(small->resource=="hwanhee1.corum_ranked/CorumSansBlack24.fnt"&&small->text=="작은 글씨");
    // A width fit settles the atlas for its largest scale first.
    fitLabelWidth(small,1000,0.9f,0.3f);assert(small->resource=="hwanhee1.corum_ranked/CorumSansBlack48.fnt"&&same(small->displayedWidth(),240*0.9f));
    // The countdown digits and GD's own fonts keep their only atlas.
    setLabelScale(digit,0.2f);assert(digit->resource=="CorumSansCountdown.fnt");
    setLabelScale(fallback,0.2f);assert(fallback->resource=="goldFont.fnt");
    // An atlas that cannot load is never switched to: the label keeps its own.
    auto* kept=label("Kept",1.5f,{0,0},{255,255,255},FontWeight::ExtraBold,"hwanhee1.corum_ranked/CorumSansBold.fnt");
    CCLabelBMFont::missing="hwanhee1.corum_ranked/CorumSansBold48.fnt";
    setLabelScale(kept,0.9f);assert(kept->resource=="hwanhee1.corum_ranked/CorumSansBold.fnt"&&same(kept->displayedWidth(),240*0.9f));
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

