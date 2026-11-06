#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace corum::ranked {

// Debug builds only: "Send recent logs" from the main menu sends the current
// and the two previous Geode log files to the server in small text chunks.
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
class DebugLogUploader {
public:
    static DebugLogUploader& get();
    void start();
    bool busy() const { return m_busy; }
    // Empty when idle; otherwise the progress, done or failure caption.
    std::string const& status() const { return m_status; }
private:
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
