#include "RankAssetPackManager.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr auto kDownloadTimeout = std::chrono::seconds(120);

std::vector<std::string> const& requiredAssets() {
    static const std::vector<std::string> assets = {
        "corum-trophy-unranked.png", "corum-trophy-red.png", "corum-trophy-aqua.png",
        "corum-trophy-bronze.png", "corum-trophy-silver.png", "corum-trophy-gold.png",
        "rank-unranked-body.png", "rank-unranked-core.png",
        "rank-unranked-fragment-tl.png", "rank-unranked-fragment-tr.png",
        "rank-unranked-fragment-bl.png", "rank-unranked-fragment-br.png",
        "rank-red-body.png", "rank-red-core.png", "rank-red-spikes.png",
        "rank-aqua-body.png", "rank-aqua-core.png", "rank-aqua-orbit-back.png", "rank-aqua-orbit-front.png",
        "rank-bronze-body.png", "rank-bronze-core.png", "rank-bronze-frame-top.png",
        "rank-bronze-frame-bottom.png", "rank-bronze-frame-left.png", "rank-bronze-frame-right.png",
        "rank-silver-body.png", "rank-silver-core.png", "rank-silver-wing-left.png",
        "rank-silver-wing-right.png", "rank-silver-crystal-top.png",
        "rank-gold-body.png", "rank-gold-core.png", "rank-gold-side-left.png", "rank-gold-side-right.png",
        "rank-gold-lower-crest.png", "rank-gold-crown.png", "rank-gold-radiant-ornament.png",
        "rank-transition-background.png", "rank-transition-geometry-overlay.png",
        "fx-rank-radial-glow-white.png", "fx-rank-broken-ring-white.png",
        "fx-rank-shockwave-circle-white.png", "fx-rank-shockwave-diamond-white.png",
        "fx-rank-light-sweep-white.png", "fx-rank-energy-column-white.png",
        "fx-rank-glint-white.png", "fx-rank-crack-white.png",
        "fx-silver-halo.png", "fx-silver-orbit.png", "fx-silver-crystal-sheen.png",
        "fx-gold-halo-inner.png", "fx-gold-halo-outer.png", "fx-gold-radial-rays.png", "fx-gold-crown-aura.png",
        "placement-grade-s.png", "placement-grade-a.png", "placement-grade-b.png",
        "placement-grade-c.png", "placement-grade-d.png",
    };
    return assets;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

// Small self-contained SHA-256 implementation keeps the asset verifier portable
// across every Geode target and avoids guessing a platform-specific crypto API.
class Sha256 final {
public:
    void update(unsigned char const* data, std::size_t length) {
        for (std::size_t i = 0; i < length; ++i) {
            m_data[m_dataLength++] = data[i];
            if (m_dataLength == 64) {
                transform();
                m_bitLength += 512;
                m_dataLength = 0;
            }
        }
    }

    std::array<unsigned char, 32> finish() {
        auto i = m_dataLength;
        if (m_dataLength < 56) {
            m_data[i++] = 0x80;
            while (i < 56) m_data[i++] = 0;
        } else {
            m_data[i++] = 0x80;
            while (i < 64) m_data[i++] = 0;
            transform();
            std::memset(m_data.data(), 0, 56);
        }
        m_bitLength += static_cast<std::uint64_t>(m_dataLength) * 8;
        for (int shift = 56, index = 56; shift >= 0; shift -= 8, ++index) {
            m_data[index] = static_cast<unsigned char>(m_bitLength >> shift);
        }
        transform();

        std::array<unsigned char, 32> hash {};
        for (std::size_t word = 0; word < 8; ++word) {
            hash[word * 4] = static_cast<unsigned char>((m_state[word] >> 24) & 0xff);
            hash[word * 4 + 1] = static_cast<unsigned char>((m_state[word] >> 16) & 0xff);
            hash[word * 4 + 2] = static_cast<unsigned char>((m_state[word] >> 8) & 0xff);
            hash[word * 4 + 3] = static_cast<unsigned char>(m_state[word] & 0xff);
        }
        return hash;
    }

private:
    static constexpr std::array<std::uint32_t, 64> K = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };

    static std::uint32_t rotr(std::uint32_t x, std::uint32_t n) { return (x >> n) | (x << (32 - n)); }

    void transform() {
        std::array<std::uint32_t, 64> m {};
        for (std::size_t i = 0, j = 0; i < 16; ++i, j += 4) {
            m[i] = (static_cast<std::uint32_t>(m_data[j]) << 24) |
                   (static_cast<std::uint32_t>(m_data[j + 1]) << 16) |
                   (static_cast<std::uint32_t>(m_data[j + 2]) << 8) |
                   static_cast<std::uint32_t>(m_data[j + 3]);
        }
        for (std::size_t i = 16; i < 64; ++i) {
            auto s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
            auto s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
            m[i] = m[i - 16] + s0 + m[i - 7] + s1;
        }
        auto a=m_state[0], b=m_state[1], c=m_state[2], d=m_state[3];
        auto e=m_state[4], f=m_state[5], g=m_state[6], h=m_state[7];
        for (std::size_t i = 0; i < 64; ++i) {
            auto s1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
            auto ch = (e & f) ^ ((~e) & g);
            auto temp1 = h + s1 + ch + K[i] + m[i];
            auto s0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
            auto maj = (a & b) ^ (a & c) ^ (b & c);
            auto temp2 = s0 + maj;
            h=g; g=f; f=e; e=d+temp1; d=c; c=b; b=a; a=temp1+temp2;
        }
        m_state[0]+=a; m_state[1]+=b; m_state[2]+=c; m_state[3]+=d;
        m_state[4]+=e; m_state[5]+=f; m_state[6]+=g; m_state[7]+=h;
    }

    std::array<unsigned char,64> m_data {};
    std::size_t m_dataLength = 0;
    std::uint64_t m_bitLength = 0;
    std::array<std::uint32_t,8> m_state {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };
};

} // namespace

bool RankAssetPackMetadata::configured() const {
    return !assetPackVersion.empty() && !driveFileId.empty() && sizeBytes > 0 &&
        RankAssetPackManager::validVersionText(assetPackVersion) &&
        RankAssetPackManager::validSha256Text(sha256);
}

RankAssetPackManager& RankAssetPackManager::get() {
    static RankAssetPackManager instance;
    return instance;
}

RankAssetPackManager::RankAssetPackManager() {
    loadCurrent();
}

void RankAssetPackManager::configure(RankAssetPackMetadata metadata) {
    ++m_downloadGeneration;
    m_downloadRequest.cancel();
    m_metadata = std::move(metadata);
    loadCurrent();
    if (!m_metadata.configured()) {
        if (m_activeDir.empty()) m_view.state = RankAssetPackState::Disabled;
        return;
    }
    if (m_view.activeVersion == m_metadata.assetPackVersion && requiredAssetsPresent(m_activeDir)) {
        m_view.state = RankAssetPackState::Ready;
        m_view.error.clear();
        return;
    }
    m_view.state = RankAssetPackState::Disabled;
}

void RankAssetPackManager::prepare() {
    if (!m_metadata.configured()) {
        if (m_activeDir.empty()) m_view.state = RankAssetPackState::Disabled;
        return;
    }
    if (activateExisting(m_metadata.assetPackVersion)) return;
    downloadConfiguredPack();
}

RankAssetPackView const& RankAssetPackManager::view() const { return m_view; }
bool RankAssetPackManager::ready() const { return m_view.state == RankAssetPackState::Ready; }

std::filesystem::path RankAssetPackManager::rootDir() const {
    auto* mod = Mod::get();
    return mod ? mod->getSaveDir() / "ranked-assets" : std::filesystem::path();
}

std::filesystem::path RankAssetPackManager::activeDirFor(std::string const& version) const {
    return rootDir() / version;
}

bool RankAssetPackManager::validSha256Text(std::string_view text) {
    if (text.size() != 64) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool RankAssetPackManager::validVersionText(std::string_view text) {
    if (text.empty() || text.size() > 64) return false;
    return std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isalnum(c) || c == '.' || c == '-' || c == '_';
    });
}

bool RankAssetPackManager::hasZipSignature(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char,4> header {};
    input.read(reinterpret_cast<char*>(header.data()), 4);
    if (input.gcount() != 4) return false;
    return header[0] == 'P' && header[1] == 'K' &&
        ((header[2] == 3 && header[3] == 4) || (header[2] == 5 && header[3] == 6) ||
         (header[2] == 7 && header[3] == 8));
}

bool RankAssetPackManager::requiredAssetsPresent(std::filesystem::path const& directory) {
    if (directory.empty()) return false;
    std::error_code ec;
    for (auto const& asset : requiredAssets()) {
        auto const path = directory / asset;
        if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
        if (std::filesystem::file_size(path, ec) == 0 || ec) return false;
    }
    return true;
}

std::string RankAssetPackManager::sha256File(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    Sha256 sha;
    std::array<unsigned char, 64 * 1024> buffer {};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        auto const count = input.gcount();
        if (count > 0) sha.update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (!input.eof()) return {};
    auto const digest = sha.finish();
    std::ostringstream stream;
    stream << std::hex << std::setfill('0');
    for (auto byte : digest) stream << std::setw(2) << static_cast<unsigned>(byte);
    return stream.str();
}

void RankAssetPackManager::loadCurrent() {
    m_activeDir.clear();
    m_view.activeVersion.clear();
    m_view.error.clear();
    auto const current = rootDir() / "current.json";
    auto json = geode::utils::file::readJson(current);
    if (json.isErr()) {
        m_view.state = RankAssetPackState::Disabled;
        return;
    }
    auto const version = json.unwrap()["assetPackVersion"].asString().unwrapOr("");
    if (!validVersionText(version)) {
        m_view.state = RankAssetPackState::Disabled;
        return;
    }
    if (activateExisting(version)) return;
    m_view.state = RankAssetPackState::Disabled;
}

bool RankAssetPackManager::activateExisting(std::string const& version) {
    if (!validVersionText(version)) return false;
    auto const dir = activeDirFor(version);
    if (!requiredAssetsPresent(dir)) return false;
    m_activeDir = dir;
    m_view.activeVersion = version;
    m_view.state = RankAssetPackState::Ready;
    m_view.error.clear();
    return true;
}

void RankAssetPackManager::fail(std::string reason) {
    m_view.state = RankAssetPackState::Failed;
    m_view.error = std::move(reason);
    log::warn("Ranked asset pack: {}", m_view.error);
}

void RankAssetPackManager::downloadConfiguredPack() {
    if (m_view.state == RankAssetPackState::Downloading || !m_metadata.configured()) return;
    auto const root = rootDir();
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) { fail("cannot create ranked-assets cache directory"); return; }

    auto const zip = root / "download.tmp";
    std::filesystem::remove(zip, ec);
    auto const generation = ++m_downloadGeneration;
    auto const url = "https://drive.google.com/uc?export=download&id=" + m_metadata.driveFileId;
    web::WebRequest request;
    request.timeout(kDownloadTimeout);
    request.followRedirects(true);
    m_view.state = RankAssetPackState::Downloading;
    m_view.error.clear();
    m_downloadRequest.spawn(request.get(url), [this, generation, zip](web::WebResponse response) {
        if (generation != m_downloadGeneration) return;
        if (!response.ok()) { fail(fmt::format("Drive download HTTP {}", response.code())); return; }
        std::string contentType = response.header("content-type").value_or("");
        contentType = lower(std::move(contentType));
        if (contentType.find("text/html") != std::string::npos) {
            fail("Drive returned HTML/login/quota response instead of ZIP");
            return;
        }
        auto saved = response.into(zip);
        if (saved.isErr()) { fail("failed to save asset ZIP"); return; }
        finishDownloadedPack(zip);
    });
}

void RankAssetPackManager::finishDownloadedPack(std::filesystem::path const& zipPath) {
    std::error_code ec;
    auto const actualSize = std::filesystem::file_size(zipPath, ec);
    if (ec || actualSize != m_metadata.sizeBytes) { fail("asset ZIP size mismatch"); return; }
    if (!hasZipSignature(zipPath)) { fail("asset download is not a ZIP archive"); return; }
    auto const digest = lower(sha256File(zipPath));
    if (digest.empty() || digest != lower(m_metadata.sha256)) { fail("asset ZIP SHA-256 mismatch"); return; }

    auto const root = rootDir();
    auto const tempDir = root / (m_metadata.assetPackVersion + ".tmp");
    auto const finalDir = activeDirFor(m_metadata.assetPackVersion);
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::create_directories(tempDir, ec);
    if (ec) { fail("cannot create asset extraction directory"); return; }
    auto extracted = geode::utils::file::Unzip::intoDir(zipPath, tempDir, false);
    if (extracted.isErr()) { std::filesystem::remove_all(tempDir, ec); fail("asset ZIP extraction failed"); return; }
    if (!requiredAssetsPresent(tempDir)) {
        std::filesystem::remove_all(tempDir, ec);
        fail("asset ZIP is missing required Ranked visuals");
        return;
    }

    // Activation happens only after the complete temp directory has verified.
    std::filesystem::remove_all(finalDir, ec);
    ec.clear();
    std::filesystem::rename(tempDir, finalDir, ec);
    if (ec) { fail("verified asset pack could not be activated"); return; }

    matjson::Value marker;
    marker["assetPackVersion"] = m_metadata.assetPackVersion;
    marker["sha256"] = lower(m_metadata.sha256);
    marker["sizeBytes"] = static_cast<std::int64_t>(m_metadata.sizeBytes);
    auto written = geode::utils::file::writeStringSafe(root / "current.json", marker.dump());
    if (written.isErr()) { fail("asset pack activated but current.json could not be written"); return; }
    std::filesystem::remove(zipPath, ec);
    (void)activateExisting(m_metadata.assetPackVersion);
    log::info("Ranked asset pack activated: {}", m_metadata.assetPackVersion);
}

std::filesystem::path RankAssetPackManager::resourcePath(std::string_view filename) const {
    if (!m_activeDir.empty()) {
        auto const external = m_activeDir / std::string(filename);
        std::error_code ec;
        if (std::filesystem::is_regular_file(external, ec) && !ec) return external;
    }
    auto* mod = Mod::get();
    if (!mod) return {};
    auto const bundled = mod->getResourcesDir() / std::string(filename);
    std::error_code ec;
    if (std::filesystem::is_regular_file(bundled, ec) && !ec) return bundled;
    return {};
}

std::filesystem::path rankedVisualResourcePath(std::string_view filename) {
    return RankAssetPackManager::get().resourcePath(filename);
}

} // namespace corum::ranked::ui
