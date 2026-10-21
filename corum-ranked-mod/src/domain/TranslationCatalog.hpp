#pragma once
#include "TranslationCatalog.generated.hpp"
#include <algorithm>
#include <string_view>

namespace corum::ranked {
inline CatalogTranslation const* catalogTranslation(std::string_view key) {
    auto const found=std::lower_bound(kTranslations.begin(),kTranslations.end(),key,
        [](auto const& entry,std::string_view search){return std::string_view(entry.key)<search;});
    return found!=kTranslations.end() && found->key==key ? &*found : nullptr;
}
// Catalog pointers have static lifetime, including labels cached across frames.
inline char const* catalogText(std::string_view key,bool korean,char const* fallback="") {
    if(auto const* entry=catalogTranslation(key))return korean?entry->korean:entry->english;
    return fallback;
}
}
