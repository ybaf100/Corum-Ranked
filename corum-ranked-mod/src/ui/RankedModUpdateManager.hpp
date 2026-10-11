#pragma once
#include "../domain/ModUpdatePolicy.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

namespace corum::ranked::ui {
enum class ModUpdateState { Ready, Required, Downloading, Verifying, RestartRequired, Failed };
struct ModUpdateView {
    ModUpdateState state = ModUpdateState::Ready;
    std::string installedVersion, targetVersion, error;
    float progress = 0;
};
class RankedModUpdateManager final {
public:
    static RankedModUpdateManager& get();
    void configure(ModUpdateMetadata metadata, bool descriptorPresent);
    void download();
    void restart();
    ModUpdateView const& view() const { return m_view; }
    bool ready() const { return m_view.state == ModUpdateState::Ready; }
    bool canDownload() const { return m_downloadAllowed; }
private:
    void fail(std::string error);
    void verifyAndInstall(std::filesystem::path const& file, std::uint64_t generation);
    ModUpdateMetadata m_metadata;
    ModUpdateView m_view;
    std::uint64_t m_generation = 0;
    bool m_downloadAllowed = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
};
}
