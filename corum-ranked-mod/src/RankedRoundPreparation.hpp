#pragma once

#include "RankedLevelPreparation.hpp"
#include "domain/RoundResourcePolicy.hpp"
#include <Geode/binding/LevelDownloadDelegate.hpp>
#include <Geode/binding/MusicDownloadDelegate.hpp>
#include <map>

class PlayLayer;
class SongInfoObject;

namespace corum::ranked {

// Drives only an exact running Solo Ranked native entry while its scene-local
// update callbacks are suspended. Authorization remains in the PlayLayer gate.
void pumpHeldRankedEntry(PlayLayer* layer);

struct RoundPreparationView {
    bool mapReady = false;
    bool audioReady = false;
    int songsDone = 0, songsTotal = 0, sfxDone = 0, sfxTotal = 0;
    float audioProgress = 0;
    std::string error;
};

// Process lifetime: no download callback owns a temporary UI/PlayLayer pointer.
// All callbacks only latch results; pump() drains native ownership before retry.
class RankedRoundPreparation final : public cocos2d::CCNode, public LevelDownloadDelegate,
    public MusicDownloadDelegate {
public:
    static RankedRoundPreparation& get();
    void update(float dt) override;
    void pump();
    void retry();
    RoundPreparationView const& view() const { return m_view; }
    bool ready() const;
    bool launch(cocos2d::CCScene* origin);
    void setLowDetail(bool enabled);
    bool beginNativeQuit(PlayLayer* play);
    void endNativeQuit();
    bool returnFromNativeQuit(GJGameLevel* level);
    void drainNativeReturn();
    bool nativeReturnPending(PlayLayer* play)const;

    void levelDownloadFinished(GJGameLevel* level) override;
    void levelDownloadFailed(int response) override;
    void loadSongInfoFinished(SongInfoObject* info) override;
    void loadSongInfoFailed(int id, GJSongError error) override;
    void downloadSongFinished(int id) override;
    void downloadSongFailed(int id, GJSongError error) override;
    void downloadSFXFinished(int id) override;
    void downloadSFXFailed(int id, GJSongError error) override;
private:
    struct ResourceTicket {
        LevelPreparationContext context;
        double began = 0;
        unsigned quiet = 0;
        bool completed = false;
        bool discarded = false;
    };
    bool nativeActive(RoundResourceKey key) const;
    bool resourceReady(RoundResourceKey key) const;
    void complete(RoundResourceKey key);
    void issue(RoundResourceKey key, double now);
    void pumpMap(double now);
    void pumpAudio(GJGameLevel* level, double now);
    LevelPreparationContext m_context, m_downloadContext;
    RoundPreparationView m_view;
    bool m_pendingMap = false, m_completedMap = false, m_discardedMap = false;
    bool m_downloadFailure = false, m_mapTimeoutReported = false, m_musicWatching = false;
    unsigned m_mapQuiet = 0;
    double m_nextPumpAt = 0;
    double m_mapBegan = 0, m_contextBegan = 0, m_urlNextAt = 0;
    bool m_urlPending = false;
    double m_urlBegan = 0;
    unsigned m_urlQuiet = 0, m_urlAttempts = 0;
    RoundResourceRetry m_mapRetry;
    std::string m_mapKey;
    geode::Ref<GJGameLevel> m_downloaded;
    std::vector<int> m_songs, m_sfx;
    std::map<RoundResourceKey, ResourceTicket> m_tickets;
    std::map<RoundResourceKey, RoundResourceRetry> m_retries;
    geode::WeakRef<PlayLayer> m_play;
    geode::WeakRef<cocos2d::CCScene> m_origin,m_returnScene;
    std::optional<geode::WeakRef<cocos2d::CCScene>> m_nativePauseRestoreOrigin;
    geode::Ref<GJGameLevel> m_launchedLevel;
    // GD's native AudioEffectsLayer uses the shared texture cache during setup.
    // Keep its owner alive across native quit and the retained round UI.
    geode::Ref<cocos2d::CCTextureCache> m_nativeTextureCache;
    bool m_quitting = false, m_returnQueued = false, m_launching = false, m_returnPopped = false;
    std::uint64_t m_launchGeneration = 0;
    double m_nextLaunchAt = 0;
    float m_timer = 0;
    bool m_lowDetail = false;
};

// Scope covers vanilla teardown, including GameManager return after PlayLayer's
// singleton has been cleared. It does not change ordinary gameplay navigation.
struct ScopedRankedRoundQuit {
    bool owned;
    explicit ScopedRankedRoundQuit(PlayLayer* play) : owned(RankedRoundPreparation::get().beginNativeQuit(play)) {}
    ~ScopedRankedRoundQuit() { if (owned) RankedRoundPreparation::get().endNativeQuit(); }
};

} // namespace corum::ranked
