import test from 'node:test';
import assert from 'node:assert/strict';
import {writeFileSync, mkdtempSync, rmSync} from 'node:fs';
import {join, resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {execFileSync} from 'node:child_process';

// Deterministic structure-aware fuzzing of every decoder that reads data from
// other players or the server (live poses, world events, cosmetics) and of the
// on-disk replay parser. Mutated inputs must be rejected or decoded; under
// AddressSanitizer/UBSan any out-of-bounds read, overflow or abort fails.
const root = resolve(new URL('..', import.meta.url).pathname);

const cpp = `
#include "${root}/src/replay/SurvivalReplayCodec.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
namespace geode::log{template<class...T>void warn(T const&...){}template<class...T>void info(T const&...){}}
using namespace corum::ranked;
using Json = matjson::Value;

struct Rng {
    std::uint64_t state;
    std::uint64_t next() { state ^= state << 13; state ^= state >> 7; state ^= state << 17; return state; }
    std::size_t below(std::size_t n) { return n ? static_cast<std::size_t>(next() % n) : 0; }
};

static char const* const kKeys[] = {"x","y","rotation","mode","dual","p2","camera","cameraX","cameraY","cameraZoom",
    "cameraLayers","discontinuity","events","type","ordinal","outputs","group","kind","tick","elapsed","dt","queued",
    "remaining","preInputs","progress","event","records","sequence","previous","sha256","footer","attemptId",
    "randomSeed","replayRandSeed","icons","colors","glow","trail","name","beforeMode","afterMode","fromX","toX"};

Json scalar(Rng& r) {
    switch (r.below(12)) {
        case 0: return Json(nullptr);
        case 1: return Json(r.below(2) == 1);
        case 2: return Json(static_cast<std::int64_t>(r.next()));
        case 3: return Json(-static_cast<std::int64_t>(r.below(1u << 20)));
        case 4: return Json(std::numeric_limits<double>::max());
        case 5: return Json(-std::numeric_limits<double>::max());
        case 6: return Json(std::numeric_limits<double>::denorm_min());
        case 7: return Json(static_cast<double>(r.below(200000)) / 7.0 - 1000.0);
        case 8: { std::string s; auto n = r.below(48); for (std::size_t i = 0; i < n; ++i) s.push_back(static_cast<char>(r.next())); return Json(s); }
        case 9: { static char const* modes[] = {"CUBE","SHIP","BALL","UFO","WAVE","ROBOT","SPIDER","SWING","", "cube"}; return Json(std::string(modes[r.below(10)])); }
        case 10: return Json(std::string(r.below(4096), 'a'));
        default: return Json(static_cast<std::int64_t>(r.below(64)));
    }
}

Json randomValue(Rng& r, int depth) {
    if (depth > 6 || r.below(3) == 0) return scalar(r);
    if (r.below(2) == 0) {
        auto array = Json::array();
        auto n = r.below(8);
        for (std::size_t i = 0; i < n; ++i) array.push(randomValue(r, depth + 1));
        return array;
    }
    auto object = Json::object();
    auto n = r.below(8);
    for (std::size_t i = 0; i < n; ++i) object[kKeys[r.below(std::size(kKeys))]] = randomValue(r, depth + 1);
    return object;
}

void mutate(Json& value, Rng& r, int depth) {
    if ((value.isObject() || value.isArray()) && value.size() > 0 && depth < 8 && r.below(4) != 0) {
        auto index = r.below(value.size());
        auto it = value.begin() + static_cast<std::ptrdiff_t>(index);
        if (value.isObject() && r.below(6) == 0) {
            auto key = it->getKey();
            if (key) { value.erase(*key); return; }
        }
        mutate(*it, r, depth + 1);
        return;
    }
    if (value.isObject() && r.below(3) == 0) { value[kKeys[r.below(std::size(kKeys))]] = randomValue(r, depth); return; }
    if (value.isArray() && r.below(3) == 0) { value.push(randomValue(r, depth)); return; }
    value = randomValue(r, depth);
}

void decodeAll(Json const& value) {
    (void)decodeSpectatorPose(value);
    (void)decodeSpectatorRandomEvent(value);
    (void)decodeSpectatorCosmetics(value);
    (void)replay::recordValid(value);
    (void)replay::metadataValid(value);
    if (auto pose = decodeSpectatorPose(value)) {
        // Whatever decodes must re-encode and decode to the same accepted shape.
        auto again = decodeSpectatorPose(encodeSpectatorPose(*pose));
        assert(again.has_value());
        assert(std::isfinite(again->x) && std::isfinite(again->y));
    }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    std::filesystem::path scratch = argv[1];
    std::vector<Json> seeds;
    for (int mode = 0; mode < 8; ++mode) {
        SpectatorGhostPose pose;
        pose.x = 100.0f + mode; pose.y = 200.0f; pose.rotation = 45.0f * mode; pose.mode = static_cast<SpectatorGameMode>(mode);
        pose.camera = SpectatorCameraOrigin{10.0f, 20.0f};
        auto encoded = encodeSpectatorPose(pose);
        assert(decodeSpectatorPose(encoded).has_value());
        seeds.push_back(encoded);
        Json record = encoded; record["kind"] = "pose"; record["tick"] = std::int64_t(mode); record["elapsed"] = 0.5 * mode; record["progress"] = 10.0 * mode;
        seeds.push_back(record);
    }
    {
        Json frame; frame["kind"] = "frame"; frame["tick"] = std::int64_t(1); frame["elapsed"] = 0.1; frame["dt"] = 1.0 / 240.0;
        seeds.push_back(frame);
        Json terminal; terminal["kind"] = "terminal"; terminal["tick"] = std::int64_t(2); terminal["elapsed"] = 0.2; terminal["reason"] = "DEATH"; terminal["progress"] = 42.0;
        seeds.push_back(terminal);
    }
    Rng rng{0x9E3779B97F4A7C15ull};
    std::size_t decoded = 0;
    for (int round = 0; round < 1500; ++round) {
        auto value = seeds[rng.below(seeds.size())];
        auto steps = 1 + rng.below(6);
        for (std::size_t i = 0; i < steps; ++i) mutate(value, rng, 0);
        decodeAll(value);
        if (decodeSpectatorPose(value)) ++decoded;
        // Byte-level damage through the real parser as well.
        auto text = value.dump(matjson::NO_INDENTATION);
        for (std::size_t flips = rng.below(4); flips > 0 && !text.empty(); --flips) text[rng.below(text.size())] = static_cast<char>(rng.next());
        if (auto parsed = Json::parse(text)) decodeAll(parsed.unwrap());
        // On-disk replay parser: random lines, truncated or damaged JSON blocks.
        if (round % 10 == 0) {
            auto file = scratch / ("fuzz-" + std::to_string(round) + ".replay");
            {
                std::ofstream out(file, std::ios::binary);
                auto lines = 1 + rng.below(5);
                for (std::size_t i = 0; i < lines; ++i) {
                    auto line = randomValue(rng, 0).dump(matjson::NO_INDENTATION);
                    if (rng.below(3) == 0 && !line.empty()) line.resize(rng.below(line.size()));
                    out << line << (rng.below(5) ? "\\n" : "");
                }
            }
            for (bool recover : {false, true}) {
                try { (void)replay::decode(file, recover); } catch (std::exception const&) {}
            }
            std::filesystem::remove(file);
        }
    }
    assert(decoded > 0); // the mutator still reaches the accepting paths
    return 0;
}
`;

test('decoders of peer/server/replay data reject or accept fuzzed inputs without memory errors', () => {
    const json = process.env.CORUM_MATJSON_SOURCE, result = process.env.CORUM_RESULT_INCLUDE;
    assert(json && result, 'Set CORUM_MATJSON_SOURCE and CORUM_RESULT_INCLUDE to the pinned SDK dependencies');
    const dir = mkdtempSync(join(tmpdir(), 'corum-decoder-fuzz-'));
    try {
        writeFileSync(join(dir, 'test.cpp'), cpp);
        execFileSync('g++', ['-std=c++23', '-pthread', '-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=undefined',
            '-fno-omit-frame-pointer', '-Wall', '-Wextra', '-Werror', '-Wno-misleading-indentation',
            '-isystem', json + '/include', '-isystem', result, join(dir, 'test.cpp'),
            ...['value', 'impl', 'parser', 'dump', 'external/dragonbox'].map(p => json + '/src/' + p + '.cpp'),
            '-o', join(dir, 'fuzz')], {stdio: 'pipe'});
        execFileSync(join(dir, 'fuzz'), [dir], {stdio: 'pipe', env: {...process.env, ASAN_OPTIONS: 'detect_leaks=0:abort_on_error=1'}});
    } finally { rmSync(dir, {recursive: true, force: true}); }
});
