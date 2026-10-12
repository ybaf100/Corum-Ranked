#include "RankAssetPackManager.hpp"
#include "../domain/Sha256.hpp"

#include <Geode/utils/file.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>
#include <set>
#include <thread>

using namespace geode::prelude;

namespace corum::ranked::ui {
namespace {

constexpr auto kDownloadTimeout = std::chrono::seconds(120);

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}



} // namespace

RankAssetPackManager& RankAssetPackManager::get() {
    // Workers and HTTP completions can outlive every Ranked scene.
    static auto* instance = new RankAssetPackManager;
    return *instance;
}

RankAssetPackManager::RankAssetPackManager() { loadCurrent(); }

void RankAssetPackManager::configure(RankAssetPackMetadata metadata) {
    metadata.sha256 = lower(std::move(metadata.sha256));
    if (metadata == m_metadata) return;
    ++m_downloadGeneration;
    m_downloadRequest.cancel();
    m_metadata = std::move(metadata);
    m_view.targetVersion = m_metadata.assetPackVersion;
    m_view.update = !m_view.activeVersion.empty();
    m_view.progress = 0;
    m_view.verifying = false;
    m_view.error.clear();
    if (!m_metadata.enabled) { m_view.state = RankAssetPackState::Disabled; return; }
    if (!m_metadata.configured()) { fail("invalid server asset pack metadata"); return; }
    if (m_view.activeVersion == m_metadata.assetPackVersion && m_activeHash == m_metadata.sha256 &&
        !m_activeDir.empty() && verifyDirectory(m_activeDir, m_metadata.assetPackVersion)) {
        m_view.state = RankAssetPackState::Ready;
        m_view.progress = 1;
        return;
    }
    if (activateExisting(m_metadata)) return;
    m_view.state = RankAssetPackState::Disabled; // required, awaiting one Download All click
}

void RankAssetPackManager::prepare() {
    if (!m_metadata.configured() || ready() || m_view.state == RankAssetPackState::Failed) return;
    downloadConfiguredPack();
}

void RankAssetPackManager::retry() {
    if (!m_metadata.configured() || m_view.state == RankAssetPackState::Downloading) return;
    m_view.state = RankAssetPackState::Disabled;
    m_view.error.clear();
    prepare();
}

RankAssetPackView const& RankAssetPackManager::view() const { return m_view; }
bool RankAssetPackManager::ready() const { return m_view.state == RankAssetPackState::Ready; }

std::filesystem::path RankAssetPackManager::rootDir() const {
    return Mod::get()->getSaveDir() / "ranked-assets";
}
std::filesystem::path RankAssetPackManager::activeDirFor(RankAssetPackMetadata const& metadata) const {
    return rootDir() / (metadata.assetPackVersion + "-" + metadata.sha256);
}
bool RankAssetPackManager::validSha256Text(std::string_view text) { return rankAssetHashValid(text); }
bool RankAssetPackManager::validVersionText(std::string_view text) { return rankAssetVersionValid(text); }

bool RankAssetPackManager::hasZipSignature(std::filesystem::path const& path) {
    std::ifstream input(path, std::ios::binary);
    std::array<unsigned char,4> header {};
    input.read(reinterpret_cast<char*>(header.data()), 4);
    return input.gcount() == 4 && header[0] == 'P' && header[1] == 'K' && header[2] == 3 && header[3] == 4;
}

bool RankAssetPackManager::requiredAssetsPresent(std::filesystem::path const& directory) {
    if (directory.empty()) return false;
    std::error_code ec;
    for (auto const& asset : kRankVisualFiles) {
        auto const path = directory / std::string(asset);
        if (std::filesystem::is_symlink(path, ec) || ec || !std::filesystem::is_regular_file(path, ec) || ec) return false;
        auto const size = std::filesystem::file_size(path, ec);
        if (ec || size == 0 || size > 8 * 1024 * 1024) return false;
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

bool RankAssetPackManager::verifyDirectory(std::filesystem::path const& directory, std::string const& version) {
    if (!validVersionText(version) || !requiredAssetsPresent(directory)) return false;
    std::error_code ec;
    auto const manifest = directory / "asset-manifest.json";
    if (std::filesystem::is_symlink(manifest, ec) || ec || std::filesystem::file_size(manifest, ec) > 128 * 1024 || ec) return false;
    auto const json = geode::utils::file::readJson(manifest);
    if (json.isErr()) return false;
    auto const& root = json.unwrap();
    if (root["schema"].asInt().unwrapOr(0) != 1 || root["assetPackVersion"].asString().unwrapOr("") != version) return false;
    auto const files = root["files"].asArray();
    if (files.isErr() || files.unwrap().size() != kRankVisualFiles.size() + 1) return false;
    std::set<std::string> found;
    for (auto const& entry : files.unwrap()) {
        auto const name = entry["name"].asString().unwrapOr("");
        if (!rankAssetEntryAllowed(name) || name == "asset-manifest.json" || !found.insert(name).second) return false;
        auto const hash = lower(entry["sha256"].asString().unwrapOr(""));
        auto const size = entry["sizeBytes"].asInt().unwrapOr(0);
        auto const path = directory / name;
        if (!validSha256Text(hash) || size <= 0 || size > 8 * 1024 * 1024 ||
            std::filesystem::is_symlink(path, ec) || ec ||
            std::filesystem::file_size(path, ec) != static_cast<std::uintmax_t>(size) || ec || sha256File(path) != hash) return false;
    }
    return found.count("GLOBED-LICENSE.txt") && std::all_of(kRankVisualFiles.begin(), kRankVisualFiles.end(),
        [&found](auto name) { return found.count(std::string(name)) != 0; });
}

void RankAssetPackManager::loadCurrent() {
    auto json = geode::utils::file::readJson(rootDir() / "current.json");
    if (json.isErr()) return;
    RankAssetPackMetadata stored;
    stored.enabled = true;
    stored.assetPackVersion = json.unwrap()["assetPackVersion"].asString().unwrapOr("");
    stored.sha256 = lower(json.unwrap()["sha256"].asString().unwrapOr(""));
    auto const size = json.unwrap()["sizeBytes"].asInt().unwrapOr(0);
    stored.sizeBytes = size > 0 ? static_cast<std::uintmax_t>(size) : 0;
    if (!stored.configured()) return;
    // Preserve installation identity even if a file was removed: show Update,
    // rather than claiming this device has never installed a pack.
    m_view.activeVersion = stored.assetPackVersion;
    (void)activateExisting(stored);
}

bool RankAssetPackManager::activateExisting(RankAssetPackMetadata const& metadata) {
    if (!metadata.configured()) return false;
    auto const dir = activeDirFor(metadata);
    if (!verifyDirectory(dir, metadata.assetPackVersion)) return false;
    m_activeDir = dir;
    m_activeHash = metadata.sha256;
    m_view.activeVersion = metadata.assetPackVersion;
    m_view.state = RankAssetPackState::Ready;
    m_view.progress = 1;
    m_view.error.clear();
    return true;
}

void RankAssetPackManager::fail(std::string reason) {
    m_view.state = RankAssetPackState::Failed;
    m_view.verifying = false;
    m_view.error = std::move(reason);
    log::warn("Ranked asset pack: {}", m_view.error);
}

void RankAssetPackManager::downloadConfiguredPack() {
    if (m_view.state == RankAssetPackState::Downloading || !m_metadata.configured()) return;
    std::error_code ec;
    std::filesystem::create_directories(rootDir(), ec);
    if (ec) { fail("cannot create Ranked asset cache"); return; }
    auto const generation = ++m_downloadGeneration;
    auto const zip = rootDir() / fmt::format("download-{}.zip.tmp", generation);
    web::WebRequest request;
    request.timeout(kDownloadTimeout);
    request.followRedirects(true);
    request.onProgress([this, generation](web::WebProgress const& progress) {
        if (generation != m_downloadGeneration || m_view.verifying) return;
        m_view.progress = std::clamp(static_cast<float>(progress.downloaded()) /
            static_cast<float>(m_metadata.sizeBytes), 0.0f, 0.95f);
    });
    m_view.state = RankAssetPackState::Downloading;
    m_view.progress = 0;
    m_view.verifying = false;
    m_view.error.clear();
    // Only the server's exact release tag is used. No latest API or per-image requests.
    m_downloadRequest.spawn(request.get(m_metadata.downloadUrl()), [this, generation, zip](web::WebResponse response) {
        if (generation != m_downloadGeneration) return;
        if (!response.ok()) { fail(fmt::format("asset release HTTP {}", response.code())); return; }
        // Geode 5.10.1 accumulates headers across redirects; header() returns
        // the first value. GitHub's 302 is text/html even when the final body
        // is the correct ZIP. Verify the saved payload's size/signature/SHA
        // below rather than accepting or rejecting it by those headers.
        auto saved = response.into(zip);
        if (saved.isErr()) { fail("failed to save asset ZIP"); return; }
        finishDownloadedPack(zip, generation);
    });
}

void RankAssetPackManager::finishDownloadedPack(std::filesystem::path const& zipPath, std::uint64_t generation) {
    auto const metadata = m_metadata;
    auto const tempDir = rootDir() / fmt::format("extract-{}.tmp", generation);
    m_view.verifying = true;
    m_view.progress = 0.95f;
    // Hashing and extracting 25 MB must not freeze the download UI. Workers have
    // immutable metadata/paths and no UI node, delegate or gameplay references.
    std::thread([this, metadata, generation, zipPath, tempDir] {
        std::string error;
        std::error_code ec;
        auto const actualSize = std::filesystem::file_size(zipPath, ec);
        if (ec || actualSize != metadata.sizeBytes) error = "asset ZIP size mismatch";
        else if (!hasZipSignature(zipPath)) error = "asset download is not a ZIP";
        else if (sha256File(zipPath) != metadata.sha256) error = "asset ZIP SHA-256 mismatch";
        else {
            auto opened = geode::utils::file::Unzip::create(zipPath);
            if (opened.isErr()) error = "asset ZIP could not be opened";
            else {
                auto& archive = opened.unwrap();
                auto const entries = archive.getEntries();
                std::set<std::string> names;
                if (entries.size() != kRankVisualFiles.size() + 2) error = "unexpected asset ZIP entries";
                for (auto const& entry : entries) {
                    auto const name = entry.generic_string();
                    if (!rankAssetEntryAllowed(name) || !names.insert(name).second) error = "unsafe or duplicate asset ZIP entry";
                }
                if (error.empty()) {
                    std::filesystem::remove_all(tempDir, ec);
                    std::filesystem::create_directories(tempDir, ec);
                    if (ec) error = "cannot create asset extraction directory";
                    else for (auto const& entry : entries) {
                        // Flat allowlist, explicit output path: archive paths never choose destinations.
                        if (archive.extractTo(entry, tempDir / entry.filename()).isErr()) {
                            error = "asset ZIP extraction failed"; break;
                        }
                    }
                    if (error.empty() && !verifyDirectory(tempDir, metadata.assetPackVersion))
                        error = "asset files or manifest failed verification";
                }
            }
        }
        geode::queueInMainThread([this, metadata, generation, tempDir, zipPath, error = std::move(error)] {
            activateDownloadedPack(tempDir, zipPath, metadata, generation, error);
        });
    }).detach();
}

void RankAssetPackManager::activateDownloadedPack(std::filesystem::path const& tempDir,
    std::filesystem::path const& zipPath, RankAssetPackMetadata const& metadata,
    std::uint64_t generation, std::string error) {
    std::error_code ec;
    std::filesystem::remove(zipPath, ec);
    if (generation != m_downloadGeneration || metadata != m_metadata) {
        std::filesystem::remove_all(tempDir, ec); return;
    }
    if (!error.empty()) { std::filesystem::remove_all(tempDir, ec); fail(std::move(error)); return; }
    auto const finalDir = activeDirFor(metadata);
    auto backupDir = finalDir; backupDir += ".backup";
    std::filesystem::remove_all(backupDir, ec);
    auto const hadExisting = std::filesystem::exists(finalDir, ec);
    if (hadExisting) {
        std::filesystem::rename(finalDir, backupDir, ec);
        if (ec) { fail("could not preserve installed pack"); return; }
    }
    std::filesystem::rename(tempDir, finalDir, ec);
    if (ec) {
        if (hadExisting) std::filesystem::rename(backupDir, finalDir, ec);
        fail("verified pack could not be activated"); return;
    }
    matjson::Value marker;
    marker["assetPackVersion"] = metadata.assetPackVersion;
    marker["sha256"] = metadata.sha256;
    marker["sizeBytes"] = static_cast<std::int64_t>(metadata.sizeBytes);
    if (geode::utils::file::writeStringSafe(rootDir() / "current.json", marker.dump()).isErr()) {
        std::filesystem::remove_all(finalDir, ec);
        if (hadExisting) std::filesystem::rename(backupDir, finalDir, ec);
        fail("could not save installed pack marker"); return;
    }
    auto const previousDir = m_activeDir;
    m_activeDir = finalDir; m_activeHash = metadata.sha256;
    m_view.activeVersion = metadata.assetPackVersion;
    m_view.state = RankAssetPackState::Ready;
    m_view.progress = 1; m_view.verifying = false; m_view.error.clear();
    std::filesystem::remove_all(backupDir, ec);
    if (!previousDir.empty() && previousDir != finalDir) std::filesystem::remove_all(previousDir, ec);
    log::info("Ranked asset pack activated: {}", metadata.assetPackVersion);
}

std::filesystem::path RankAssetPackManager::resourcePath(std::string_view filename) const {
    if (!rankAssetEntryAllowed(filename) || !filename.ends_with(".png")) return {};
    if (!m_activeDir.empty()) {
        auto const external = m_activeDir / std::string(filename);
        std::error_code ec;
        if (std::filesystem::is_regular_file(external, ec) && !ec) return external;
    }
    // Legacy installations can still render their bundled images. New packages
    // exclude all 61 PNGs and renderers have built-in GD fallback sprites.
    auto const bundled = Mod::get()->getResourcesDir() / std::string(filename);
    std::error_code ec;
    return std::filesystem::is_regular_file(bundled, ec) && !ec ? bundled : std::filesystem::path{};
}

std::filesystem::path rankedVisualResourcePath(std::string_view filename) {
    return RankAssetPackManager::get().resourcePath(filename);
}

} // namespace corum::ranked::ui
