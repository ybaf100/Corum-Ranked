#pragma once
#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace corum::ranked {

// The debug fixed-map battle's order: R1, R2, R3 and the Death Match map, by
// canonical id ("" = drawn at random). Picking a map fills the next empty
// slot; picking a placed map takes it out again.
struct FixedMapOrder {
    static constexpr std::size_t Slots = 4, Deathmatch = 3;
    std::array<std::string, Slots> slots{};

    // The slot the map went to, or -1 when it was taken out / nothing is free.
    int pick(std::string const& id) {
        if (id.empty()) return -1;
        for (auto& slot : slots) if (slot == id) { slot.clear(); return -1; }
        for (std::size_t i = 0; i < Slots; ++i) if (slots[i].empty()) { slots[i] = id; return static_cast<int>(i); }
        return -1;
    }
    void clear(std::size_t slot) { if (slot < Slots) slots[slot].clear(); }
    [[nodiscard]] bool empty() const { return std::all_of(slots.begin(), slots.end(), [](auto const& s) { return s.empty(); }); }
    [[nodiscard]] int slotOf(std::string const& id) const {
        for (std::size_t i = 0; i < Slots; ++i) if (!id.empty() && slots[i] == id) return static_cast<int>(i);
        return -1;
    }
    // Rounds as sent to the server: up to the last chosen round ("" between).
    [[nodiscard]] std::vector<std::string> rounds() const {
        std::size_t last = 0;
        for (std::size_t i = 0; i < Deathmatch; ++i) if (!slots[i].empty()) last = i + 1;
        return {slots.begin(), slots.begin() + static_cast<long>(last)};
    }
    [[nodiscard]] std::string const& deathmatch() const { return slots[Deathmatch]; }
    [[nodiscard]] static char const* label(std::size_t slot) {
        return slot == 0 ? "R1" : slot == 1 ? "R2" : slot == 2 ? "R3" : "DM";
    }
};

} // namespace corum::ranked
