#pragma once

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

struct RankAssetPackMetadata {
    std::string assetPackVersion;
    std::string driveFileId;
    std::string sha256;
    std::uintmax_t sizeBytes = 0;

    [[nodiscard]] bool configured() const;
};

struct RankAssetPackView {
    RankAssetPackState state = RankAssetPackState::Disabled;
    std::string activeVersion;
    std::string error;
};

class RankAssetPackManager final {
public:
    static RankAssetPackManager& get();

    // beta.14 intentionally ships without production Drive metadata. This method
    // is ready for the authoritative config values once they are supplied.
    void configure(RankAssetPackMetadata metadata);
    void prepare();

    [[nodiscard]] RankAssetPackView const& view() const;
    [[nodiscard]] bool ready() const;
    [[nodiscard]] std::filesystem::path resourcePath(std::string_view filename) const;

    [[nodiscard]] static bool validSha256Text(std::string_view text);
    [[nodiscard]] static bool validVersionText(std::string_view text);
    [[nodiscard]] static bool hasZipSignature(std::filesystem::path const& path);
    [[nodiscard]] static bool requiredAssetsPresent(std::filesystem::path const& directory);
    [[nodiscard]] static std::string sha256File(std::filesystem::path const& path);

private:
    RankAssetPackManager();

    [[nodiscard]] std::filesystem::path rootDir() const;
    [[nodiscard]] std::filesystem::path activeDirFor(std::string const& version) const;
    [[nodiscard]] bool activateExisting(std::string const& version);
    void loadCurrent();
    void fail(std::string reason);
    void downloadConfiguredPack();
    void finishDownloadedPack(std::filesystem::path const& zipPath);

    RankAssetPackMetadata m_metadata;
    RankAssetPackView m_view;
    std::filesystem::path m_activeDir;
    std::uint64_t m_downloadGeneration = 0;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_downloadRequest;
};

// Shared renderer lookup. External verified cache wins; bundled assets remain
// the permanent offline fallback.
[[nodiscard]] std::filesystem::path rankedVisualResourcePath(std::string_view filename);

} // namespace corum::ranked::ui
