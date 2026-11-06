#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace corum::ranked {

// Debug builds only: "Send logs" from the main menu sends the Geode logs and
// crash logs the player picked to the server in small text chunks.
struct DebugLogChunk {
    int fileIndex = 0;
    std::string fileName;
    int part = 0, totalParts = 1;
    std::string content;
};

// Keeps only valid UTF-8 (invalid bytes become '?') so the JSON body parses.
std::string debugLogSanitize(std::string_view raw);
// The last `tailBytes` of a log, cut at a line start, in chunks of at most
// `chunkBytes` that never split a UTF-8 sequence.
std::vector<DebugLogChunk> debugLogChunks(int fileIndex, std::string const& fileName, std::string_view raw,
    std::size_t tailBytes = 1'000'000, std::size_t chunkBytes = 40'000);

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
} // namespace corum::ranked
#include <cstdint>
#include <filesystem>
namespace corum::ranked {

// One Geode log or crash log the player can pick in the Send Logs popup.
struct DebugLogFile {
    std::filesystem::path path;
    std::string name;
    bool crash = false;
    bool current = false; // this game session's own log
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified{};
};

inline constexpr std::size_t DebugLogMaxFiles = 6;

// Main-menu popup: pick game logs and crash logs to send.
void showDebugLogPopup();

class DebugLogUploader {
public:
    static DebugLogUploader& get();
    // Newest Geode logs and crash logs (up to 6 each), newest first.
    static std::vector<DebugLogFile> listFiles();
    void start(std::vector<DebugLogFile> const& files);
    bool busy() const { return m_busy; }
    // Empty when idle; otherwise the progress, done or failure caption.
    std::string const& status() const { return m_status; }
    void clearStatus() { if (!m_busy) m_status.clear(); }
private:
    void fail(std::string const& reason);
    void sendNext();
    std::vector<DebugLogChunk> m_chunks;
    std::size_t m_next = 0;
    int m_files = 0, m_accountId = 0;
    std::string m_username;
    std::string m_uploadId, m_status;
    bool m_busy = false;
};
#endif

} // namespace corum::ranked
