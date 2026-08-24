#pragma once

#include <Geode/Geode.hpp>

#include <array>
#include <string>
#include <string_view>

namespace corum::ranked::i18n {

// Edit Korean wording in this file. CORUM RANKED, ROUND labels, Qualifying,
// tier names, LP, player names, and map names intentionally remain English.
// If a newly edited Korean sentence introduces a Hangul syllable not already
// used below, add its Unicode code point to the CorumSansSemiBold,
// CorumSansBold, and CorumSansBlack charsets in mod.json as well.
enum class Language {
    English,
    Korean,
};

struct Translation {
    std::string_view key;
    std::string_view korean;
};

inline constexpr std::array kKoreanTranslations {
    Translation{"competitive-ladder", "경쟁 1대1 래더"},
    Translation{"history", "기록"},
    Translation{"match-history", "경기 기록"},
    Translation{"recent-matches", "최근 경쟁 경기"},
    Translation{"your-rank", "내 랭크"},
    Translation{"win-streak", "연승"},
    Translation{"loss-streak", "연패"},
    Translation{"seasonal-progress", "시즌 래더 진행도"},
    Translation{"placement", "배치"},
    Translation{"placement-matches", "배치 경기"},
    Translation{"placed", "배치 완료"},
    Translation{"promotion", "승급"},
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
    Translation{"you", "나"},
    Translation{"opponent", "상대"},
    Translation{"score", "점수"},
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
