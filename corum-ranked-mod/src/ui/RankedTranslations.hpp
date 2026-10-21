#pragma once

#include <Geode/Geode.hpp>

#include "../domain/TranslationCatalog.hpp"
#include <string>
#include <string_view>

namespace corum::ranked::i18n {

// Translation source: ranked/translations/ui.json and achievements.json.
// Build generation is shared by the native client and server achievement catalog.
enum class Language { English, Korean };

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
    if(auto const* entry=catalogTranslation(key))return isKorean()?entry->korean:entry->english;
    return std::string(english); // External/server messages unknown to this build remain readable.
}

inline char const* literal(std::string_view key) {
    return catalogText(key,isKorean());
}

inline std::string insertValue(std::string pattern,std::string const& value) {
    if(auto const at=pattern.find("{}");at!=std::string::npos)pattern.replace(at,2,value);
    return pattern;
}

inline std::string runtimeStatus(std::string const& status) {
    auto const prefix=std::string_view("Searching for an opponent (rating range +/-");
    auto const suffix=std::string_view(")...");
    if(status.starts_with(prefix) && status.ends_with(suffix))
        return insertValue(text("runtime-search-range",status),status.substr(prefix.size(),status.size()-prefix.size()-suffix.size()));
    return text(status,status);
}

inline std::string streak(int count,bool winning) {
    return insertValue(text(winning?"streak-win":"streak-loss","{}"),std::to_string(count));
}
inline std::string promotionMatch(int winsRequired) {
    return insertValue(text("promotion-required","{}"),std::to_string(winsRequired));
}
inline std::string attemptOfThree(int attempt) {
    return insertValue(text("attempt-of-three","{}"),std::to_string(attempt));
}
inline std::string waitingForPlayer(std::string const& player) {
    return insertValue(text("waiting-for-player","{}"),player);
}

} // namespace corum::ranked::i18n
