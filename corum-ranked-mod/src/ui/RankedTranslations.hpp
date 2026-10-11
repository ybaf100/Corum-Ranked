#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <string>
#include <string_view>

namespace corum::ranked::i18n {

// Edit Korean wording in this file. CORUM RANKED, ROUND labels, Qualifying,
// tier names, LP, player names, map names, and literal POOL 1..6 labels intentionally remain English.
// If a newly edited Korean sentence introduces a Hangul syllable not already
// used below, add its Unicode code point to the CorumSansMedium,
// CorumSansSemiBold, CorumSansBold, and CorumSansBlack charsets in mod.json.
enum class Language {
    English,
    Korean,
};

struct Translation {
    std::string_view key;
    std::string_view korean;
};

inline constexpr std::array kKoreanTranslations {
    Translation{"leaderboard", "리더보드"},
    Translation{"leaderboard-player", "플레이어"},
    Translation{"leaderboard-status", "상태"},
    Translation{"leaderboard-tier", "티어"},
    Translation{"leaderboard-refresh", "새로고침"},
    Translation{"leaderboard-loading", "리더보드 불러오는 중..."},
    Translation{"leaderboard-error", "리더보드를 불러오지 못했습니다. 새로고침을 누르세요."},
    Translation{"leaderboard-empty", "아직 플레이어가 없습니다."},
    Translation{"mod-update", "모드 업데이트"},
    Translation{"mod-restart-ready", "업데이트 완료. 재시작을 눌러 적용하세요."},
    Translation{"mod-verifying", "모드를 다운로드하고 검증 중..."},
    Translation{"mod-update-required", "랭크전에 입장하려면 모드를 업데이트하세요."},
    Translation{"mod-update-download", "업데이트 다운로드"},
    Translation{"restart", "재시작"},
    Translation{"additional-downloads", "추가 다운로드"},
    Translation{"session-other-device", "다른 기기에서 접속 중"},
    Translation{"session-takeover-detail", "다른 기기의 접속을 끊고 여기서 입장합니다.\n진행 중인 경기는 이어서 복구합니다."},
    Translation{"session-disconnect-other", "다른 기기 접속 끊기"},
    Translation{"admin-unlock", "도전과제 모두 해금"},
    Translation{"admin-lp", "LP 및 티어 설정"},
    Translation{"admin-mmr", "MMR 설정"},
    Translation{"admin-both", "LP · MMR · 티어 설정"},
    Translation{"admin-delete-bots", "봇 전부 삭제"},
    Translation{"admin-history", "플레이어 전체 초기화"},
    Translation{"admin-reset", "랭크 DB 초기화"},
    Translation{"admin-password-needed", "디버그 비밀번호를 입력하세요."},
    Translation{"admin-player-needed", "플레이어 닉네임을 입력하세요."},
    Translation{"admin-working", "변경 사항 적용 중..."},
    Translation{"admin-audio", "곡별 공통 볼륨"},
    Translation{"admin-audio-title", "곡별 볼륨 (모든 유저)"},
    Translation{"admin-audio-scope", "랭크전 메뉴 음악 · 서버에 저장"},
    Translation{"admin-audio-save", "모든 유저에게 저장"},
    Translation{"admin-audio-reload", "다시 불러오기"},
    Translation{"admin-audio-empty", "설정된 랭크전 음악이 없습니다."},
    Translation{"admin-audio-unsaved", "저장을 누르면 모든 유저에게 적용됩니다."},
    Translation{"admin-audio-failed", "곡별 볼륨을 불러오거나 저장하지 못했습니다."},
    Translation{"admin-audio-saved", "저장됨. 모든 유저에게 약 5초 내 전달됩니다."},
    Translation{"admin-audio-menu", "로비"},
    Translation{"admin-audio-promotion", "승급전 로비"},
    Translation{"admin-audio-warning", "강등 경고 로비"},
    Translation{"admin-audio-match", "1·2 라운드"},
    Translation{"admin-audio-deathmatch", "3 라운드·데스매치 대기"},
    Translation{"admin-audio-win", "승리 결과"},
    Translation{"admin-audio-lose", "패배 결과"},
    Translation{"debug", "디버그"},
    Translation{"debug-bot", "봇 매치"},
    Translation{"debug-spectate", "관전"},
    Translation{"settings", "설정"},
    Translation{"info", "정보"},
    Translation{"player-info", "플레이어 정보"},
    Translation{"info-stars", "별 개수"},
    Translation{"info-moons", "달 개수"},
    Translation{"info-demons", "데몬 개수"},
    Translation{"info-unavailable", "조회 불가"},
    Translation{"info-unranked", "순위 없음"},
    Translation{"info-unlinked", "계정 연결 안 됨"},
    Translation{"info-bot-reference", "봇전 참고 프로필: CoJong"},
    Translation{"forum-refresh", "포럼 기록 갱신"},
    Translation{"forum-updating", "포럼 기록 갱신 중..."},
    Translation{"forum-update-failed", "갱신 실패. 다시 시도하세요."},
    Translation{"forum-cache-kept", "API 연결 불가 · 저장된 기록 유지"},
    Translation{"ranked-volume", "랭크전 메뉴 배경음악"},
    Translation{"ranked-volume-scope", "로비·매칭·결과 화면 배경음악"},
    Translation{"competitive-ladder", "경쟁 1대1 래더"},
    Translation{"history", "기록"},
    Translation{"achievements", "도전과제"},
    Translation{"achievement-subtitle", "진행도 · AP · 칭호"},
    Translation{"claimable", "수령 가능"},
    Translation{"claimed", "수령 완료"},
    Translation{"locked", "잠김"},
    Translation{"completed", "달성 완료"},
    Translation{"set-title", "칭호 지정"},
    Translation{"select-title-version", "도전과제 칭호 버전 선택"},
    Translation{"clear-title", "칭호 해제"},
    Translation{"achievement-unlocked", "도전과제 달성"},
    Translation{"achievement-upgraded", "도전과제 단계 상승"},
    Translation{"achievement-maxed", "도전과제 최고 단계"},
    Translation{"available", "수령 가능"},
    Translation{"match-history", "경기 기록"},
    Translation{"recent-matches", "최근 경쟁 경기"},
    Translation{"your-rank", "내 랭크"},
    Translation{"win-streak", "연승"},
    Translation{"loss-streak", "연패"},
    Translation{"seasonal-progress", "시즌 래더 진행도"},
    Translation{"placement", "배치"},
    Translation{"placement-matches", "배치 경기"},
    Translation{"placement-setup", "배치 설정"},
    Translation{"placement-count", "배치 경기 횟수 선택"},
    Translation{"placement-count-lock", "3회 또는 5회를 선택하세요. 시작 후에는 변경할 수 없습니다."},
    Translation{"placement-three-stage-warning", "경고: 3회 배치는 배치 최대점이 낮아집니다. 최대 배치는 AQUA입니다. 시작 후 변경할 수 없습니다."},
    Translation{"placement-five-stage-info", "5회 · 10개 맵 · 전체 배치 범위 · 시작 후 변경할 수 없습니다."},
    Translation{"select-placement-count-first", "먼저 3회 또는 5회를 선택하세요."},
    Translation{"select-placement-pool-first", "먼저 시작 POOL을 선택하세요."},
    Translation{"select-starting-pool", "시작 POOL 선택"},
    Translation{"pool-choice-explanation", "이 선택은 STAGE 1의 시작 POOL만 정합니다. 최종 랭크는 전체 결과로 결정됩니다."},
    Translation{"start-placement", "배치 시작"},
    Translation{"choose-first-pool", "첫 STAGE POOL을 선택하세요"},
    Translation{"reference-loading", "기준 맵 불러오는 중..."},
    Translation{"skip-placement", "배치 건너뛰기"},
    Translation{"irreversible-choice", "되돌릴 수 없는 선택"},
    Translation{"skip-placement-warning", "배치를 즉시 종료하며 되돌릴 수 없습니다."},
    Translation{"skip-placement-result", "RED - 150 LP에 배치됩니다."},
    Translation{"cancel", "취소"},
    Translation{"surrender", "항복"},
    Translation{"surrender-round", "라운드 항복"},
    Translation{"surrender-match", "경기 항복"},
    Translation{"surrender-choice", "현재 라운드 또는 전체 경기를 항복할 수 있습니다."},
    Translation{"surrender-score-kept", "이미 획득한 점수는 유지됩니다."},
    Translation{"placement-skip-map", "맵 스킵"},
    Translation{"placement-skip-map-warning", "지금까지 획득한 점수로 이 맵만 종료할까요?"},
    Translation{"placement-skip-syncing", "현재 시도 점수 저장 중..."},
    Translation{"placement-skip-sending", "현재 맵 스킵 중..."},
    Translation{"placement-skip-retrying", "연결 문제 - 맵 스킵 재시도 중..."},
    Translation{"placement-skip-accepted", "맵을 건너뛰었습니다"},
    Translation{"placement-skip-rejected", "맵 스킵 실패. 현재 맵을 확인하고 다시 시도하세요."},
    Translation{"surrender-confirm", "항복 확인"},
    Translation{"surrender-round-warning", "현재 라운드를 패배로 종료합니다. 항복하시겠습니까?"},
    Translation{"surrender-match-warning", "전체 경기를 패배로 종료합니다. 항복하시겠습니까?"},
    Translation{"surrender-waiting-attempt", "시도 결과 동기화 후 항복을 전송합니다."},
    Translation{"surrender-sending", "항복 전송 중..."},
    Translation{"surrender-retrying", "연결 오류 · 항복 재전송 중..."},
    Translation{"surrender-accepted", "항복 처리 완료"},
    Translation{"opponent-surrender-round", "상대가 라운드에 항복했습니다."},
    Translation{"opponent-surrender-match", "상대가 경기에서 항복했습니다."},
    Translation{"surrender-rejected", "항복할 수 없습니다. 경기 상태를 확인 후 다시 시도하세요."},
    Translation{"match-pause", "경기 일시정지"},
    Translation{"pause-timeout-warning", "0초가 되면 경기 전체가 항복 처리됩니다."},
    Translation{"forum-refresh-cooldown", "다음 갱신까지"},
    Translation{"confirm-skip", "건너뛰고 배치"},
    Translation{"solo-placement", "싱글 배치"},
    Translation{"opening-exact-level", "정확한 GD 레벨을 여는 중..."},
    Translation{"placement-map-live", "맵 진행 중 · 등급은 시도 종료 후 변경"},
    Translation{"final-attempt-progress", "마지막 시도 진행 중"},
    Translation{"complete", "완료"},
    Translation{"server-result", "서버 결과"},
    Translation{"map-score", "맵 점수"},
    Translation{"next-map", "다음 맵"},
    Translation{"show-stage-result", "STAGE 결과 보기"},
    Translation{"result", "결과"},
    Translation{"stage-grade", "STAGE 등급"},
    Translation{"all-placement-maps-complete", "전체 10개 맵 완료"},
    Translation{"next", "다음"},
    Translation{"final-placement", "최종 배치"},
    Translation{"next-stage", "다음 STAGE"},
    Translation{"placement-complete", "배치 완료"},
    Translation{"placement-skipped", "배치 건너뜀"},
    Translation{"ten-maps-five-stages", "10개 맵 · 5개 STAGE"},
    Translation{"placement-cancelled", "배치 취소"},
    Translation{"server-run", "서버 권위 RUN"},
    Translation{"close", "닫기"},
    Translation{"placed", "배치 완료"},
    Translation{"promotion", "승급"},
    Translation{"promoted", "승급했습니다!"},
    Translation{"demotion-next-loss", "다음 경기에서 패배하면 강등될 수 있습니다."},
    Translation{"jukebox-required", "Jukebox 모드가 필요합니다!"},
    Translation{"promotion-match", "승급전"},
    Translation{"promotion-failed", "승급 실패"},
    Translation{"promoted", "승급"},
    Translation{"demoted", "강등"},
    Translation{"demotion-warning", "강등 경고"},
    Translation{"rating", "레이팅"},
    Translation{"matchmaking", "매칭"},
    Translation{"start-queue", "매칭 시작"},
    Translation{"find-match", "1대1 상대 찾기"},
    Translation{"searching", "상대 검색 중..."},
    Translation{"leave-queue", "매칭 취소"},
    Translation{"syncing", "동기화 중..."},
    Translation{"connection-issue", "연결 오류"},
    Translation{"retry", "다시 시도"},
    Translation{"mod-check-required", "모드 확인 필요"},
    Translation{"ranked-access-blocked", "랭크 이용 차단"},
    Translation{"environment-warning-body", "현재 모드 상태로는 랭크를 이용할 수 없습니다."},
    Translation{"environment-warning-action", "비허용 모드는 끄고 필수 모드는 설치·활성화한 뒤 게임을 다시 시작하세요."},
    Translation{"unauthorized-active-mods", "허용되지 않은 활성 모드"},
    Translation{"missing-required-mods", "필수 모드가 없거나 꺼져 있음"},
    Translation{"incompatible-mod-versions", "버전이 맞지 않는 모드"},
    Translation{"cbf-settings-required", "CBF 설정 확인"},
    Translation{"installed-version", "설치"},
    Translation{"required-version", "필요"},
    Translation{"version-or-newer", "이상"},
    Translation{"version-or-older", "이하"},
    Translation{"cbf-setting-off", "다음 CBF 설정을 꺼 주세요"},
    Translation{"cbf-setting-invalid", "CBF 설정이 랭크 요구사항과 다릅니다."},
    Translation{"retry-check", "다시 검사"},
    Translation{"you", "나"},
    Translation{"opponent", "상대"},
    Translation{"score", "점수"},
    Translation{"assets-new-download", "이미지팩 신규 다운로드"},
    Translation{"assets-update", "이미지팩 업데이트"},
    Translation{"assets-verifying", "이미지팩을 확인하고 설치 중입니다..."},
    Translation{"assets-verifying-short", "검증 중"},
    Translation{"assets-one-download", "필요한 리소스를 한 번에 다운로드하세요."},
    Translation{"assets-basic-ui", "기본 화면"},
    Translation{"assets-images", "이미지"},
    Translation{"assets-music", "음악"},
    Translation{"jukebox-song-scope", "이 맵에서 사용할 곡을 변경합니다"},
    Translation{"jukebox-open", "Jukebox 열기"},
    Translation{"jukebox-waiting", "Jukebox 곡 정보 준비 중..."},
    Translation{"ranked-resources", "랭크 리소스"},
    Translation{"resources-required", "추가 리소스가 필요합니다."},
    Translation{"downloading-resources", "리소스 다운로드 중"},
    Translation{"keep-open", "다운로드가 끝날 때까지 이 화면을 유지하세요."},
    Translation{"all-resources-ready", "모든 리소스 준비 완료"},
    Translation{"everything-ready", "랭크에 필요한 리소스가 모두 준비되었습니다."},
    Translation{"resources-failed", "일부 리소스 다운로드 실패"},
    Translation{"retry-before-continuing", "계속하기 전에 실패한 다운로드를 다시 시도하세요."},
    Translation{"ready", "준비"},
    Translation{"continue", "계속"},
    Translation{"tap-to-skip", "건너뛰기"},
    Translation{"retry-failed", "실패 항목 재시도"},
    Translation{"download-all", "모두 다운로드"},
    Translation{"match-found", "매칭 완료"},
    Translation{"private-ban-phase", "비공개 밴 단계"},
    Translation{"map-ban-in", "맵 밴까지"},
    Translation{"prepare-private-ban", "제외할 맵을 준비하세요"},
    Translation{"ban-map", "맵 밴"},
    Translation{"choose-map-remove", "제외할 맵 하나를 선택하세요"},
    Translation{"time-left", "남은 시간"},
    Translation{"map", "맵"},
    Translation{"difficulty", "난이도"},
    Translation{"banned", "밴 완료"},
    Translation{"banning", "밴 처리 중..."},
    Translation{"ban", "밴"},
    Translation{"private-ban-notice", "내 밴은 비공개이며 단계 종료 후 공개됩니다"},
    Translation{"death-match", "데스 매치"},
    Translation{"three-attempts", "3회 시도"},
    Translation{"series", "시리즈"},
    Translation{"clear-recorded", "클리어 기록 완료"},
    Translation{"map-ready", "맵 준비 완료"},
    Translation{"map-loading", "맵 불러오는 중"},
    Translation{"result-sync", "결과 동기화"},
    Translation{"waiting", "대기 중"},
    Translation{"audio-ready", "오디오 준비 완료"},
    Translation{"start-muted", "음소거로 시작"},
    Translation{"download-song", "노래 다운로드"},
    Translation{"song-download-only", "노래 다운로드만 가능합니다"},
    Translation{"updating-map", "맵 업데이트 중..."},
    Translation{"downloading-map", "맵 다운로드 중..."},
    Translation{"downloading", "다운로드 중..."},
    Translation{"downloading-audio", "오디오 다운로드 중..."},
    Translation{"preparation-ready", "준비 완료"},
    Translation{"download-failed", "다운로드 실패"},
    Translation{"waiting-download-response", "다운로드 응답 대기 중..."},
    Translation{"low-detail", "저사양"},
    Translation{"disable-shake", "화면 흔들림 끄기"},
    Translation{"song-downloaded", "노래 다운로드 완료"},
    Translation{"starting-without-song", "노래 없이 시작합니다"},
    Translation{"waiting-map", "맵 대기 중"},
    Translation{"waiting-song", "노래 대기 중"},
    Translation{"server-error-retrying", "서버 오류 - 재시도 중"},
    Translation{"sending-ready", "준비 전송 중..."},
    Translation{"map-payload-mismatch", "맵 데이터 불일치 - 재시도 중"},
    Translation{"map-update-incomplete", "맵 업데이트 미완료 - 재시도 중"},
    Translation{"map-download-failed", "맵 다운로드 실패"},
    Translation{"retrying", "재시도 중"},
    Translation{"map-update-stalled", "맵 업데이트 정지 - 재시도 중"},
    Translation{"play-failed", "플레이 실패 - 재시도 중"},
    Translation{"starts-in", "시작까지"},
    Translation{"syncing-clear", "클리어 동기화 중..."},
    Translation{"waiting-opponent", "상대 대기 중"},
    Translation{"attempt", "시도"},
    Translation{"of", "중"},
    Translation{"waiting-download", "내 다운로드 대기 중"},
    Translation{"sending-ready", "준비 완료 전송 중..."},
    Translation{"waiting-player", "대기 중"},
    Translation{"starting", "시작 중..."},
    Translation{"attempt-finished", "내 시도가 끝났습니다"},
    Translation{"live-progress", "실시간 진행"},
    Translation{"waiting-start", "시작 대기 중"},
    Translation{"round-resolve-hint", "진행 중인 시도가 끝나면 라운드가 결정됩니다"},
    Translation{"last-attempt-hint", "상대의 마지막 시도 시작까지"},
    Translation{"series-score", "시리즈 점수"},
    Translation{"next-map-preparing", "다음 맵 준비 중..."},
    Translation{"match-canceled", "경기 취소"},
    Translation{"invalid-no-rating", "무효 경기 - 점수 변동 없음"},
    Translation{"queue-again", "다시 매칭"},
    Translation{"match-complete", "경기 종료"},
    Translation{"maximum-rating-reward", "최대 점수 보상"},
    Translation{"win", "승리"},
    Translation{"lose", "패배"},
    Translation{"loss", "패배"},
    Translation{"draw", "무승부"},
    Translation{"victory", "승리"},
    Translation{"defeat", "패배"},
    Translation{"no-completed-matches", "완료된 경기가 없습니다"},
    Translation{"detail", "상세"},
    Translation{"live-spectator", "실시간 관전"},
    Translation{"current", "현재"},
};

inline constexpr std::string_view kSavedLanguageKey = "ranked-ui-language";

inline Language language() {
    auto const saved = geode::Mod::get()->getSavedValue<std::string>(
        std::string(kSavedLanguageKey),
        "en"
    );
    return saved == "ko" ? Language::Korean : Language::English;
}

inline bool isKorean() {
    return language() == Language::Korean;
}

inline void setLanguage(Language selected) {
    geode::Mod::get()->setSavedValue(
        std::string(kSavedLanguageKey),
        selected == Language::Korean ? std::string("ko") : std::string("en")
    );
}

inline std::string text(std::string_view key, std::string_view english) {
    if (!isKorean()) return std::string(english);
    for (auto const& translation : kKoreanTranslations) {
        if (translation.key == key) return std::string(translation.korean);
    }
    return std::string(english);
}

inline std::string streak(int count, bool winning) {
    if (!isKorean()) {
        return std::to_string(count) + "x " + (winning ? "WIN" : "LOSS") + " STREAK";
    }
    return std::to_string(count) + (winning ? text("win-streak", "WIN STREAK") : text("loss-streak", "LOSS STREAK"));
}

inline std::string promotionMatch(int winsRequired) {
    if (!isKorean()) {
        return "PROMOTION MATCH (" + std::to_string(winsRequired) + " WINS)";
    }
    return text("promotion-match", "PROMOTION MATCH") + " (" + std::to_string(winsRequired) + "승 필요)";
}

inline std::string attemptOfThree(int attempt) {
    if (!isKorean()) return "ATTEMPT  " + std::to_string(attempt) + "  OF  3";
    return std::to_string(attempt) + "번째 " + text("attempt", "ATTEMPT") + " / 3";
}

inline std::string waitingForPlayer(std::string const& player) {
    if (!isKorean()) return "WAITING FOR " + player;
    return player + " " + text("waiting-player", "WAITING");
}

} // namespace corum::ranked::i18n
