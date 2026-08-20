#include "ServerClock.hpp"

#include <algorithm>

namespace {

std::optional<int> digits(std::string_view value, std::size_t offset, std::size_t count) {
    if (offset + count > value.size()) return std::nullopt;
    int result = 0;
    for (std::size_t index = offset; index < offset + count; ++index) {
        auto const character = value[index];
        if (character < '0' || character > '9') return std::nullopt;
        result = result * 10 + (character - '0');
    }
    return result;
}

bool leapYear(int year) {
    return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

int daysInMonth(int year, int month) {
    constexpr int values[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && leapYear(year)) return 29;
    return values[month - 1];
}

std::int64_t daysFromCivil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    auto const era = (year >= 0 ? year : year - 399) / 400;
    auto const yearOfEra = static_cast<unsigned>(year - era * 400);
    auto const dayOfYear = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    auto const dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(dayOfEra) - 719468;
}

} // namespace

namespace corum::ranked {

std::optional<std::int64_t> parseIso8601Millis(std::string_view value) {
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        value[10] != 'T' || value[13] != ':' || value[16] != ':' || value.back() != 'Z') {
        return std::nullopt;
    }
    auto const year = digits(value, 0, 4);
    auto const month = digits(value, 5, 2);
    auto const day = digits(value, 8, 2);
    auto const hour = digits(value, 11, 2);
    auto const minute = digits(value, 14, 2);
    auto const second = digits(value, 17, 2);
    if (!year || !month || !day || !hour || !minute || !second) return std::nullopt;
    if (*month < 1 || *month > 12 || *day < 1 || *day > daysInMonth(*year, *month) ||
        *hour > 23 || *minute > 59 || *second > 59) {
        return std::nullopt;
    }

    int milliseconds = 0;
    if (value.size() > 20) {
        if (value[19] != '.') return std::nullopt;
        auto const fractionalCount = value.size() - 21;
        if (fractionalCount == 0 || fractionalCount > 9) return std::nullopt;
        int scale = 100;
        for (std::size_t index = 0; index < fractionalCount; ++index) {
            auto const character = value[20 + index];
            if (character < '0' || character > '9') return std::nullopt;
            if (index < 3) {
                milliseconds += (character - '0') * scale;
                scale /= 10;
            }
        }
    }

    auto const days = daysFromCivil(*year, static_cast<unsigned>(*month), static_cast<unsigned>(*day));
    auto const seconds = days * 86400 + *hour * 3600 + *minute * 60 + *second;
    return seconds * 1000 + milliseconds;
}

bool ServerClock::observe(std::string_view serverNow, std::int64_t localNowMillis) {
    auto const parsed = parseIso8601Millis(serverNow);
    if (!parsed) return false;
    m_offsetMillis = *parsed - localNowMillis;
    m_synchronized = true;
    return true;
}

bool ServerClock::synchronized() const {
    return m_synchronized;
}

std::int64_t ServerClock::serverNowMillis(std::int64_t localNowMillis) const {
    return localNowMillis + (m_synchronized ? m_offsetMillis : 0);
}

std::optional<std::int64_t> ServerClock::remainingSeconds(
    std::string_view deadlineAt,
    std::int64_t localNowMillis
) const {
    auto const remaining = remainingMillis(deadlineAt, localNowMillis);
    if (!remaining) return std::nullopt;
    return (*remaining + 999) / 1000;
}

std::optional<std::int64_t> ServerClock::remainingMillis(
    std::string_view deadlineAt,
    std::int64_t localNowMillis
) const {
    if (!m_synchronized) return std::nullopt;
    auto const deadline = parseIso8601Millis(deadlineAt);
    if (!deadline) return std::nullopt;
    return std::max<std::int64_t>(0, *deadline - serverNowMillis(localNowMillis));
}

} // namespace corum::ranked
