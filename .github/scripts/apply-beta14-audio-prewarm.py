from pathlib import Path


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected one match, got {count}")
    return text.replace(old, new, 1)

hpp_path = Path("corum-ranked-mod/src/RankedAudioManager.hpp")
hpp = hpp_path.read_text()
hpp = replace_once(
    hpp,
    '''#include <string>\n#include <vector>''',
    '''#include <string>\n#include <unordered_map>\n#include <vector>''',
    "unordered_map include",
)
hpp = replace_once(
    hpp,
    '''    void synchronizeSongCaches();\n    void stopOwnedAudio();''',
    '''    void synchronizeSongCaches();\n    void prewarmReadySounds();\n    void releasePreparedSounds();\n    [[nodiscard]] FMOD::Sound* preparedSound(int songId);\n    void stopOwnedAudio();''',
    "audio prewarm methods",
)
hpp = replace_once(
    hpp,
    '''    FMOD::Sound* m_rankedSound = nullptr;\n    FMOD::Channel* m_rankedChannel = nullptr;''',
    '''    // Prepared streaming Sounds are created while resources are prepared,\n    // never on Round/Deathmatch/Result transition callbacks.\n    std::unordered_map<int, FMOD::Sound*> m_preparedSounds;\n    FMOD::Sound* m_rankedSound = nullptr;\n    FMOD::Channel* m_rankedChannel = nullptr;''',
    "prepared sound storage",
)
hpp_path.write_text(hpp)

cpp_path = Path("corum-ranked-mod/src/RankedAudioManager.cpp")
cpp = cpp_path.read_text()
cpp = replace_once(
    cpp,
    '''    cancelActiveDownload();\n    stopOwnedAudio();\n    m_config = config;''',
    '''    cancelActiveDownload();\n    stopOwnedAudio();\n    releasePreparedSounds();\n    m_config = config;''',
    "release prepared on config change",
)
cpp = replace_once(
    cpp,
    '''    m_downloadState = resourcesReady() ? RankedResourceDownloadState::Ready : RankedResourceDownloadState::Idle;\n\n    m_switchPending = false;''',
    '''    m_downloadState = resourcesReady() ? RankedResourceDownloadState::Ready : RankedResourceDownloadState::Idle;\n    if (m_downloadState == RankedResourceDownloadState::Ready) prewarmReadySounds();\n\n    m_switchPending = false;''',
    "prewarm on configure",
)
cpp = replace_once(
    cpp,
    '''    if (m_downloadQueue.empty()) {\n        m_downloadState = m_failedSongIds.empty()\n            ? RankedResourceDownloadState::Ready\n            : RankedResourceDownloadState::Failed;\n        return;\n    }''',
    '''    if (m_downloadQueue.empty()) {\n        m_downloadState = m_failedSongIds.empty()\n            ? RankedResourceDownloadState::Ready\n            : RankedResourceDownloadState::Failed;\n        if (m_downloadState == RankedResourceDownloadState::Ready) prewarmReadySounds();\n        return;\n    }''',
    "prewarm after downloads",
)
cpp = replace_once(
    cpp,
    '''void RankedAudioManager::stopOwnedAudio() {\n    if (m_rankedChannel) {\n        m_rankedChannel->stop();\n        m_rankedChannel = nullptr;\n    }\n    if (m_rankedSound) {\n        m_rankedSound->release();\n        m_rankedSound = nullptr;\n    }''',
    '''void RankedAudioManager::stopOwnedAudio() {\n    if (m_rankedChannel) {\n        m_rankedChannel->stop();\n        m_rankedChannel = nullptr;\n    }\n    // m_rankedSound is owned by m_preparedSounds. Keep it alive across mode\n    // changes so transition frames never reopen the audio file.\n    m_rankedSound = nullptr;''',
    "retain prepared sound on stop",
)

insert_anchor = '''void RankedAudioManager::clearPlaybackState() {\n    stopOwnedAudio();\n}\n\nvoid RankedAudioManager::synchronizeSongCaches() {'''
insert_text = '''void RankedAudioManager::clearPlaybackState() {\n    stopOwnedAudio();\n}\n\nvoid RankedAudioManager::releasePreparedSounds() {\n    // Stop the active channel before releasing shared stream handles.\n    if (m_rankedChannel) {\n        m_rankedChannel->stop();\n        m_rankedChannel = nullptr;\n    }\n    m_rankedSound = nullptr;\n    for (auto& [songId, sound] : m_preparedSounds) {\n        (void)songId;\n        if (sound) sound->release();\n    }\n    m_preparedSounds.clear();\n}\n\nFMOD::Sound* RankedAudioManager::preparedSound(int songId) {\n    if (auto found = m_preparedSounds.find(songId); found != m_preparedSounds.end()) {\n        return found->second;\n    }\n    auto* engine = FMODAudioEngine::sharedEngine();\n    if (!engine || !engine->m_system) return nullptr;\n    auto const path = readySongPath(songId);\n    if (path.empty()) return nullptr;\n\n    auto const started = std::chrono::steady_clock::now();\n    FMOD::Sound* sound = nullptr;\n    auto const result = engine->m_system->createSound(\n        path.c_str(), FMOD_CREATESTREAM | FMOD_2D, nullptr, &sound\n    );\n    auto const elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(\n        std::chrono::steady_clock::now() - started\n    ).count();\n    if (result != FMOD_OK || !sound) {\n        log::error(\n            "Ranked audio prewarm failed song={} result={} duration={}us path='{}'",\n            songId, static_cast<int>(result), elapsedUs, path\n        );\n        if (sound) sound->release();\n        return nullptr;\n    }\n    m_preparedSounds[songId] = sound;\n    log::info("Ranked audio prewarmed song={} duration={}us path='{}'", songId, elapsedUs, path);\n    return sound;\n}\n\nvoid RankedAudioManager::prewarmReadySounds() {\n    if (!enabled() || !resourcesReady()) return;\n    for (auto const songId : uniqueSongIds()) {\n        (void)preparedSound(songId);\n    }\n}\n\nvoid RankedAudioManager::synchronizeSongCaches() {'''
cpp = replace_once(cpp, insert_anchor, insert_text, "insert audio prewarm implementation")

old_create = '''    FMOD_MODE mode = FMOD_CREATESTREAM | FMOD_2D;\n    mode |= resource->loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF;\n\n    FMOD::Sound* sound = nullptr;\n    auto result = engine->m_system->createSound(path.c_str(), mode, nullptr, &sound);\n    if (result != FMOD_OK || !sound) {\n        log::error(\n            "Ranked audio: createSound failed key='{}' song={} result={} path='{}'",\n            resource->key,\n            resource->songId,\n            static_cast<int>(result),\n            path\n        );\n        stopOwnedAudio();\n        return;\n    }\n    if (resource->loop) sound->setLoopCount(-1);\n\n    FMOD::Channel* channel = nullptr;\n    result = engine->m_system->playSound(sound, nullptr, true, &channel);'''
new_create = '''    // The file-backed stream was created during resource preparation. A cache\n    // miss is retained only as a recovery fallback and is timed explicitly.\n    auto* sound = preparedSound(resource->songId);\n    if (!sound) {\n        log::error(\n            "Ranked audio: prepared stream unavailable key='{}' song={} path='{}'",\n            resource->key, resource->songId, path\n        );\n        stopOwnedAudio();\n        return;\n    }\n    sound->setMode(FMOD_2D | (resource->loop ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF));\n    sound->setLoopCount(resource->loop ? -1 : 0);\n\n    auto const playStarted = std::chrono::steady_clock::now();\n    FMOD::Channel* channel = nullptr;\n    auto result = engine->m_system->playSound(sound, nullptr, true, &channel);'''
cpp = replace_once(cpp, old_create, new_create, "replace transition createSound")
cpp = replace_once(
    cpp,
    '''        sound->release();\n        return;\n    }\n\n    auto const startMs =''',
    '''        return;\n    }\n    auto const playElapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(\n        std::chrono::steady_clock::now() - playStarted\n    ).count();\n\n    auto const startMs =''',
    "do not release cached sound on play failure",
)
cpp = replace_once(
    cpp,
    '''        targetRankedVolume(),\n        retry\n    );''',
    '''        targetRankedVolume(),\n        retry\n    );\n    log::debug(\n        "Ranked audio transition playSound key='{}' song={} duration={}us prepared=true",\n        resource->key, resource->songId, playElapsedUs\n    );''',
    "log transition play cost",
)
cpp_path.write_text(cpp)
