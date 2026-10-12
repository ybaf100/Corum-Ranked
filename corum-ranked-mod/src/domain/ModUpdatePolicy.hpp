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
    std::string url{};
    bool operator==(ModUpdateMetadata const&) const = default;
    bool valid() const {
        return (channel == "debug" || channel == "release") &&
            !version.empty() && version.size() <= 40 && version.starts_with('v') &&
            std::all_of(version.begin() + 1, version.end(), [](unsigned char c) {
                return std::isalnum(c) || c == '.' || c == '-';
            }) && sha256.size() == 64 &&
            std::all_of(sha256.begin(), sha256.end(), [](unsigned char c) { return std::isxdigit(c); }) &&
            sizeBytes > 0 && sizeBytes <= kMaxModUpdateBytes && (url.empty() || validUrl(url));
    }
    std::string tag() const { return channel + "-" + version; }
    std::string filename() const { return "Corum-Ranked-" + version + "-" + channel + ".geode"; }
    std::string downloadUrl() const {
        if(!url.empty())return url;
        return "https://github.com/ybaf100/corum-ranked-downloads/releases/download/" + tag() + "/" + filename();
    }
    static bool validUrl(std::string_view value) {
        if(value.size()>2048 || !value.starts_with("https://") || value.find('#')!=std::string_view::npos ||
            value.find('\\')!=std::string_view::npos || std::any_of(value.begin(),value.end(),[](unsigned char c){return c<=32 || c>=127;}))return false;
        auto const end=value.find_first_of("/?",8);auto const authority=value.substr(8,end==std::string_view::npos?value.size()-8:end-8);
        return !authority.empty() && authority.find('@')==std::string_view::npos;
    }
};
} // namespace corum::ranked
