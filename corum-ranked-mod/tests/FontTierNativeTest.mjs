import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync, writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join} from 'node:path';
import {tmpdir} from 'node:os';

const root = new URL('..', import.meta.url).pathname;
const read = path => readFileSync(join(root, path), 'utf8');
const visuals = read('src/ui/RankedVisuals.hpp');
const fonts = JSON.parse(read('mod.json')).resources.fonts;
const method = (src, sig) => {
    const at = src.indexOf(sig); assert(at >= 0, sig);
    let depth = 0, i = src.indexOf('{', at);
    for (; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}' && --depth === 0) break; }
    return src.slice(at, i + 1);
};

test('text takes the smallest atlas at least about as large as it is drawn', () => {
    const dir = mkdtempSync(join(tmpdir(), 'corum-font-tier-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), `#include "${join(root, 'src/domain/FontMetrics.hpp')}"
#include <cassert>
#include <cmath>
using namespace corum::ranked;
bool same(float a,float b){return std::abs(a-b)<0.001f;}
int main(){
    // 1920x1080: 3.375 screen pixels per point; scale 1 draws a 54 px em.
    constexpr float p1080=1080.0f/320.0f,p1440=1440.0f/320.0f,p2160=2160.0f/320.0f;
    assert(same(corumDrawnEmPixels(1.0f,p1080),54.0f));
    // Menu copy (0.36-0.49) draws ~19-26 px: the 24 px glyphs (at most ~1.1x enlarged).
    assert(corumFontTier(0.36f,p1080,4)==24&&corumFontTier(0.49f,p1080,4)==24);
    assert(corumFontTier(0.5f,p1080,4)==48&&corumFontTier(0.98f,p1080,4)==48);
    assert(corumFontTier(1.0f,p1080,4)==128&&corumFontTier(2.0f,p1080,4)==128);
    // Larger windows move up a size sooner.
    assert(corumFontTier(0.36f,p1440,4)==24&&corumFontTier(0.4f,p1440,4)==48&&corumFontTier(0.8f,p1440,4)==128);
    assert(corumFontTier(0.24f,p2160,4)==24&&corumFontTier(0.36f,p2160,4)==48&&corumFontTier(0.5f,p2160,4)==128);
    // HD texture quality halves every atlas (12/24/64 px glyphs).
    assert(corumFontTier(0.36f,p1080,2)==48&&corumFontTier(0.5f,p1080,2)==128);
    for(float scale:{0.2f,0.36f,0.5f,0.8f,1.2f})for(float ppp:{1.0f,2.25f,p1080,p1440,p2160}){
        auto const size=corumFontTier(scale,ppp,4);
        // Never more than ~1.1x enlarged, unless even the 128 px atlas is smaller.
        assert(size==CorumFontRasterSize||static_cast<float>(size)>=corumDrawnEmPixels(scale,ppp)*0.9f-0.01f);
        // And the next smaller atlas would have been enlarged more than that.
        if(size==CorumFontMediumSize)assert(static_cast<float>(CorumFontSmallSize)<corumDrawnEmPixels(scale,ppp)*0.9f);
    }
    // Names: every size of a weight, with or without folders and quality suffixes.
    assert(corumRasterSize("hwanhee1.corum_ranked/CorumSansBold.fnt")==128);
    assert(corumRasterSize("hwanhee1.corum_ranked/CorumSansBold24.fnt")==24);
    assert(corumRasterSize("CorumSansSemiBold48-uhd.fnt")==48);
    assert(corumRasterSize("C:/Users/x48-y/geode/CorumSansBold24-hd.fnt")==24);
    assert(corumRasterSize("C:/Users/a24.b/CorumSansBold.fnt")==128);
    assert(corumRasterSize("hwanhee1.corum_ranked/CorumSansCountdown.fnt")==256);
    assert(corumTierResource("hwanhee1.corum_ranked/CorumSansBold.fnt",24)=="hwanhee1.corum_ranked/CorumSansBold24.fnt");
    assert(corumTierResource("hwanhee1.corum_ranked/CorumSansBold24.fnt",48)=="hwanhee1.corum_ranked/CorumSansBold48.fnt");
    assert(corumTierResource("hwanhee1.corum_ranked/CorumSansMedium48.fnt",128)=="hwanhee1.corum_ranked/CorumSansMedium.fnt");
    assert(corumTierResource("CorumSans.fnt",24)=="CorumSans24.fnt");
    assert(corumTierResource("hwanhee1.corum_ranked/CorumSansCountdown.fnt",24)=="hwanhee1.corum_ranked/CorumSansCountdown.fnt");
    assert(corumTierResource("goldFont.fnt",24)=="goldFont.fnt");
    // Drawn size does not depend on the atlas.
    for(auto const* name:{"CorumSansBold.fnt","CorumSansBold48.fnt","CorumSansBold24.fnt"})
        assert(same(corumFontScale(0.4f,name)*static_cast<float>(corumRasterSize(name)),0.4f*64.0f));
}
`);
        execFileSync('g++', ['-std=c++23', '-Wall', '-Wextra', '-Wpedantic', '-Werror', join(dir, 'test.cpp'), '-o', join(dir, 'test')], {stdio: 'pipe'});
        execFileSync(join(dir, 'test'), [], {stdio: 'pipe'});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});

test('every atlas a label can pick is packaged, and none is mipmapped', () => {
    for (const [, name] of method(visuals, 'inline char const* tieredFont(').matchAll(/"(CorumSans\w+)\.fnt"_spr/g)) {
        const base = name.replace(/(24|48)$/, '');
        assert.deepEqual(fonts[name], {path: fonts[base].path, size: Number(name.match(/(24|48)$/)[1]), charset: fonts[base].charset}, name);
    }
    // Trilinear mipmaps blurred small text (beta.80); the atlas size now matches the text.
    assert.doesNotMatch(visuals, /generateMipmap|GL_LINEAR_MIPMAP_LINEAR/);
    // Creation picks by the label's scale; rescaling moves between atlases.
    assert.match(method(visuals, 'inline CCLabelBMFont* label('), /resource \? resource : font\(weight, scale\)/);
    assert.match(method(visuals, 'inline CCLabelBMFont* playerNameLabel('), /font\(weight,scale\)/);
    assert.match(method(visuals, 'inline void setLabelScale('), /matchCorumFontTier\(caption, logicalScale\);\s*caption->setScale\(fontScale\(caption, logicalScale\)\);/);
    assert.match(method(visuals, 'inline void fitLabelWidth('), /matchCorumFontTier\(caption, maximumScale\);\s*caption->limitLabelWidth/);
    // fitFullLabel measures glyphs in label space: the atlas is settled first, and
    // the shrink passes only rescale.
    const full = method(visuals, 'inline void fitFullLabel(');
    assert(full.indexOf('matchCorumFontTier(text, scale)') < full.indexOf('widestLabelGlyph(text)'));
    assert.doesNotMatch(full, /setLabelScale/);
    // A swap keeps the text and lays it out again.
    assert.match(method(visuals, 'inline void matchCorumFontTier('), /setFntFile\(next\.c_str\(\)\);\s*caption->setString\(text\.c_str\(\)\);/);
});
