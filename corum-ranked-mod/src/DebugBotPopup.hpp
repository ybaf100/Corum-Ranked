#pragma once

namespace corum::ranked {

#if defined(CORUM_RANKED_DEBUG_BOT_MATCH)
void showDebugBotPasswordPopup();
// The same password, then the fixed-map battle picker (vs bot or a lobby player).
void showDebugFixedMapPasswordPopup();
#endif

} // namespace corum::ranked
