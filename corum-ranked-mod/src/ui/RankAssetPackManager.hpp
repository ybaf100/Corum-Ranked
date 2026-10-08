#pragma once

#include "../domain/RankAssetPolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace corum::ranked::ui {

enum class RankAssetPackState {
    Disabled,
    Ready,
    Downloading,
    Failed,
};

using RankAssetPackMetadata = RankAssetMetadata;

struct RankAssetPackView {
    RankAssetPackState state = RankAssetPackState::Disabled;
    std::string activeVersion;
    std::string targetVersion;
    std::string error;
    float progress = 0;
    bool verifying = false;
    bool update = false;
};

class RankAssetPackManager final {
public:
    static RankAssetPackManager& get();

    void configure(RankAssetPackMetadata metadata);
    void prepare();
    void retry();

    [[nodiscard]] RankAssetPackView const& view() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] bool required() const { return m_metadata.enabled; }
    [[nodiscard]] bool requiresDownload() const { return required() && !ready(); }
    [[nodiscard]] std::filesystem::path resourcePath(std::string_view filename) const;

    [[nodiscard]] static bool validSha256Text(std::string_view text);
    [[nodiscard]] static bool validVersionText(std::string_view text);
    [[nodiscard]] static bool hasZipSignature(std::filesystem::path const& path);
    [[nodiscard]] static bool requiredAssetsPresent(std::filesystem::path const& directory);
    [[nodiscard]] static bool verifyDirectory(std::filesystem::path const& directory, std::string const& version);
    [[nodiscard]] static std::string sha256File(std::filesystem::path const& path);

private:
    RankAssetPackManager();

    [[nodiscard]] std::filesystem::path rootDir() const;
    [[nodiscard]] std::filesystem::path activeDirFor(RankAssetPackMetadata const& metadata) const;
    [[nodiscard]] bool activateExisting(RankAssetPackMetadata const& metadata);
    void loadCurrent();
    void fail(std::string reason);
    void downloadConfiguredPack();
    void finishDownloadedPack(std::filesystem::path const& zipPath, std::uint64_t generation);
    void activateDownloadedPack(std::filesystem::path const& tempDir, std::filesystem::path const& zipPath,
        RankAssetPackMetadata const& metadata, std::uint64_t generation, std::string error);

    RankAssetPackMetadata m_metadata;
    RankAssetPackView m_view;
    std::filesystem::path m_activeDir;
    std::string m_activeHash;
    std::uint64_t m_downloadGeneration = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_downloadRequest;
};

// Verified external cache; renderers use small built-in GD fallbacks before download.
[[nodiscard]] std::filesystem::path rankedVisualResourcePath(std::string_view filename);

} // namespace corum::ranked::ui

