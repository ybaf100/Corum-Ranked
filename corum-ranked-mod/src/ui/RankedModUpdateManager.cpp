#include "RankedModUpdateManager.hpp"
#include "RankAssetPackManager.hpp"
#include <Geode/utils/file.hpp>
#include <Geode/utils/general.hpp>
#include <Geode/loader/ModMetadata.hpp>
#include <thread>
#include <atomic>

using namespace geode::prelude;
namespace corum::ranked::ui {
RankedModUpdateManager& RankedModUpdateManager::get() {
    static auto* manager = new RankedModUpdateManager;
    return *manager;
}
void RankedModUpdateManager::fail(std::string error) {
    m_view.state = ModUpdateState::Failed; m_view.error = std::move(error);
}
void RankedModUpdateManager::configure(ModUpdateMetadata metadata, bool descriptorPresent) {
    if (m_view.state == ModUpdateState::RestartRequired) return;
    ++m_generation; m_request.cancel();
    m_downloadAllowed = false;
    m_metadata = std::move(metadata);
    m_view = {};
    m_view.installedVersion = Mod::get()->getVersion().toVString();
    m_view.targetVersion = m_metadata.version;
    if (descriptorPresent && (!m_metadata.valid() || m_metadata.channel != rankedBuildChannel())) {
        fail("Invalid update metadata for this build channel"); return;
    }
    auto target = VersionInfo::parse(m_metadata.version);
    if (target.isErr()) { fail("Invalid server mod version"); return; }
    if (target.unwrap() == Mod::get()->getVersion()) return;
    if (target.unwrap() < Mod::get()->getVersion()) {
        fail("This build is newer than the server requirement. Automatic downgrade is disabled."); return;
    }
    if (!descriptorPresent) { fail("The server requires an update but has not configured its download yet."); return; }
    m_downloadAllowed = true;
    m_view.state = ModUpdateState::Required;
}
void RankedModUpdateManager::download() {
    if (!m_downloadAllowed || (m_view.state != ModUpdateState::Required && m_view.state != ModUpdateState::Failed) ||
        !m_metadata.valid() || m_metadata.channel != rankedBuildChannel()) return;
    auto const generation = ++m_generation;
    auto const directory = Mod::get()->getSaveDir() / "mod-updates";
    std::error_code ec; std::filesystem::create_directories(directory, ec);
    if (ec) { fail("Cannot create update staging directory"); return; }
    auto const file = directory / fmt::format("{}.{}.tmp", m_metadata.filename(), generation);
    m_view.state = ModUpdateState::Downloading; m_view.progress = 0; m_view.error.clear();
    m_downloadStarted=std::chrono::steady_clock::now();
    web::WebRequest request; request.timeout(std::chrono::seconds(180)); request.followRedirects(true);
    auto const size = m_metadata.sizeBytes;
    auto nextProgress=std::make_shared<std::atomic<std::int64_t>>(0);
    auto firstByte=std::make_shared<std::atomic<std::int64_t>>(0);
    request.onProgress([this, generation, size, nextProgress,firstByte](web::WebProgress const& progress) {
        auto const now=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        std::int64_t unseen=0;if(progress.downloaded()>0)firstByte->compare_exchange_strong(unseen,now);
        auto next=nextProgress->load();
        if(progress.downloaded()<size && (now<next || !nextProgress->compare_exchange_strong(next,now+100)))return;
        auto const value = std::clamp(static_cast<float>(progress.downloaded()) / static_cast<float>(size), 0.0f, 0.95f);
        Loader::get()->queueInMainThread([this, generation, value] {
            if (generation == m_generation && m_view.state == ModUpdateState::Downloading) m_view.progress = value;
        });
    });
    m_request.spawn(request.get(m_metadata.downloadUrl()), [this, generation, file,firstByte](web::WebResponse response) {
        if (generation != m_generation) return;
        auto const seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-m_downloadStarted).count();
        auto header=[&](char const* name){auto value=response.header(name);return value?std::string(*value):std::string("-");};
        auto const url=m_metadata.downloadUrl();auto const origin=url.substr(0,url.find('/',8));
        auto const startMillis=std::chrono::duration_cast<std::chrono::milliseconds>(m_downloadStarted.time_since_epoch()).count();
        log::info("[RankedModUpdate] origin={} version={} channel={} status={} bytes={} transferSeconds={:.3f} MBps={:.3f} firstProgressByteSeconds={:.3f} contentLength={} cache={} age={} cfRay={}",
            origin,m_metadata.version,m_metadata.channel,response.code(),response.data().size(),seconds,
            seconds>0?response.data().size()/seconds/1e6:0,firstByte->load()>0?(firstByte->load()-startMillis)/1000.0:-1,
            header("content-length"),header("cf-cache-status"),header("age"),header("cf-ray"));
        if (!response.ok()) { fail(fmt::format("Mod update HTTP {}", response.code())); return; }
        m_view.state = ModUpdateState::Verifying; m_view.progress = 0.95f;
        verifyAndInstall(file, generation,std::move(response));
    });
}
void RankedModUpdateManager::verifyAndInstall(std::filesystem::path const& file, std::uint64_t generation,web::WebResponse response) {
    auto const metadata = m_metadata;
    auto const binary = std::string(Mod::get()->getID()) + GEODE_PLATFORM_EXTENSION;
    std::thread([this, file, generation, metadata, binary,response=std::move(response)] {
        std::string error;
        std::error_code ec;
        auto const saveStarted=std::chrono::steady_clock::now();
        if(response.into(file).isErr())error="Cannot save mod update";
        auto const hashStarted=std::chrono::steady_clock::now();
        std::string digest;
        if(error.empty() && std::filesystem::file_size(file,ec)==metadata.sizeBytes && !ec && RankAssetPackManager::hasZipSignature(file))
            digest=RankAssetPackManager::sha256File(file);
        auto const hashEnded=std::chrono::steady_clock::now();
        if(error.empty() && digest!=metadata.sha256) {
            error = "Downloaded mod size/signature/SHA-256 mismatch";
        }
        if(error.empty()) {
            auto opened = geode::utils::file::Unzip::create(file);
            if (opened.isErr()) error = "Invalid mod package";
            else {
                auto& zip = opened.unwrap();
                auto marker = zip.extract("corum-build-channel.json");
                if (marker.isErr() || marker.unwrap().size() > 1024) error = "Missing build channel marker";
                else {
                    auto const& bytes = marker.unwrap();
                    auto json = matjson::parse(std::string(bytes.begin(), bytes.end()));
                    if (json.isErr() || json.unwrap()["channel"].asString().unwrapOr("") != metadata.channel ||
                        json.unwrap()["version"].asString().unwrapOr("") != metadata.version) error = "Wrong build channel/version in package";
                }
                if (!zip.hasEntry(binary)) error = "Package has no binary for this platform";
            }
        }
        auto const verifyEnded=std::chrono::steady_clock::now();
        log::info("[RankedModUpdate] saveMs={:.3f} sha256Ms={:.3f} verifyMs={:.3f} version={} valid={}",
            std::chrono::duration<double,std::milli>(hashStarted-saveStarted).count(),
            std::chrono::duration<double,std::milli>(hashEnded-hashStarted).count(),
            std::chrono::duration<double,std::milli>(verifyEnded-hashStarted).count(),metadata.version,error.empty());
        Loader::get()->queueInMainThread([this, file, generation, metadata, error = std::move(error)] {
            auto const installStarted=std::chrono::steady_clock::now();
            std::error_code ec;
            if (generation != m_generation) { std::filesystem::remove(file, ec); return; }
            if (!error.empty()) { std::filesystem::remove(file, ec); fail(error); return; }
            auto package = ModMetadata::createFromGeodeFile(file);
            if (package.hasErrors() || package.getID() != Mod::get()->getID() ||
                package.getVersion().toVString() != metadata.version || package.checkTargetVersions().isErr()) {
                std::filesystem::remove(file, ec); fail("Mod manifest/platform/Geode compatibility check failed"); return;
            }
            // Loaded binaries live in Geode's extracted runtime directory. Only
            // replace the package here; the supported Restart reloads its code.
            // Copy beside the current package first and keep a rollback copy.
            auto const destination = Mod::get()->getPackagePath();
            auto staged = destination; staged += ".ranked-new";
            auto backup = destination; backup += ".ranked-backup";
            std::filesystem::copy_file(file, staged, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) { fail("Cannot stage package beside installed mod"); return; }
            std::filesystem::remove(backup, ec); ec.clear();
            std::filesystem::rename(destination, backup, ec);
            if (ec) { std::filesystem::remove(staged, ec); fail("Cannot back up installed mod package"); return; }
            std::filesystem::rename(staged, destination, ec);
            if (ec) {
                std::error_code rollback; std::filesystem::rename(backup, destination, rollback);
                fail(rollback ? "Package install failed; backup is available beside the package" : "Package install failed; original restored"); return;
            }
            std::filesystem::remove(file, ec);
            m_view.state = ModUpdateState::RestartRequired; m_view.progress = 1;
            log::info("[RankedModUpdate] installMs={:.3f} version={} restartRequired=true",
                std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-installStarted).count(),metadata.version);
            // Deliberately no restart callback here. Only the player's button
            // invokes restart(), and Ranked remains gated until the new binary loads.
        });
    }).detach();
}
void RankedModUpdateManager::restart() {
    if (m_view.state == ModUpdateState::RestartRequired) geode::utils::game::restart(true);
}
}
