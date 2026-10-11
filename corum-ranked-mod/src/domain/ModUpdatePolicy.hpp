#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace corum::ranked {
inline constexpr std::uintmax_t kMaxModUpdateBytes = 128 * 1024 * 1024;
inline std::string rankedBuildChannel() {
#if defined(CORUM_RANKED_DEBUG_BOT_MATCH) || defined(CORUM_RANKED_DEBUG_SPECTATE)
    return "debug";
#else
    return "release";
#endif
}
struct ModUpdateMetadata {
    std::string version, sha256, channel;
    std::uintmax_t sizeBytes = 0;
    bool operator==(ModUpdateMetadata const&) const = default;
    bool valid() const {
        return (channel == "debug" || channel == "release") &&
            !version.empty() && version.size() <= 40 && version.starts_with('v') &&
            std::all_of(version.begin() + 1, version.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '.' || c == '-';
            }) && sha256.size() == 64 &&
            std::all_of(sha256.begin(), sha256.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
            sizeBytes > 0 && sizeBytes <= kMaxModUpdateBytes;
    }
    std::string tag() const { return channel + "-" + version; }
    std::string filename() const { return "Corum-Ranked-" + version + "-" + channel + ".geode"; }
    std::string downloadUrl() const {
        return "https://github.com/ybaf100/corum-ranked-downloads/releases/download/" + tag() + "/" + filename();
    }
};
} // namespace corum::ranked
