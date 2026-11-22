#pragma once

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
#include "RankedRuntime.hpp"

namespace corum::ranked {
// Debug: pick R1 / R2 / R3 / DM from the map pool and play them against the
// Debug Bot (with `bot`'s settings) or send them to a player idle in the lobby.
void showDebugFixedMapPopup(DebugBotMatchOptions bot);
} // namespace corum::ranked
#endif
