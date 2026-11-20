#pragma once
#include "../domain/PatchNotes.hpp"
#include "../RankedRuntime.hpp"
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>
#include <chrono>

namespace corum::ranked::ui {
// Notes are any server text, so they are drawn with the system font (the
// bundled bitmap fonts only hold the UI's own Korean syllables).
inline char const* patchNotesFont() {
#if defined(GEODE_IS_WINDOWS)
    return "Malgun Gothic";
#elif defined(GEODE_IS_MACOS) || defined(GEODE_IS_IOS)
    return "AppleSDGothicNeo-Regular";
#else
    return "sans-serif";
#endif
}

// The server's patch notes, read once per session (the update screen's first
// guide card waits for them; a failed read is tried again on the next ask).
class RankedPatchNotesCache final {
public:
    static RankedPatchNotesCache& get() { static RankedPatchNotesCache cache; return cache; }
    [[nodiscard]] bool loaded() const { return m_loaded; }
    [[nodiscard]] bool failed() const { return m_failed; }
    [[nodiscard]] std::vector<PatchNoteView> const& notes() const { return m_notes; }
    void fetch() {
        if (m_loaded || m_busy) return;
        m_busy = true; m_failed = false;
        geode::utils::web::WebRequest request;
        [](auto& q){if constexpr(requires{q.getHttpVersion();})q.version(decltype(q.getHttpVersion())::VERSION_2TLS);}(request); /* HTTP/2: see PREFER_HTTP2 note in RankedRuntime.cpp */
        request.timeout(std::chrono::seconds(20));
        m_request.spawn(request.get(RankedRuntime::get().patchNotesUrl()), [](geode::utils::web::WebResponse response) {
            geode::queueInMainThread([response = std::move(response)] {
                auto& cache = get(); cache.m_busy = false;
                auto notes = response.ok() ? patchNotesFromJson(response.json().unwrapOr(matjson::Value())) : std::vector<PatchNoteView>{};
                if (notes.empty()) { cache.m_failed = true; return; }
                cache.m_notes = std::move(notes); cache.m_loaded = true;
            });
        });
    }
private:
    std::vector<PatchNoteView> m_notes;
    bool m_loaded = false, m_failed = false, m_busy = false;
    geode::async::TaskHolder<geode::utils::web::WebResponse> m_request;
};
} // namespace corum::ranked::ui
