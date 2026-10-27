/** Default Debug fixture only. Custom native tests never become rating Matches. */
export const DEBUG_SPECTATE_LEVEL_ID = 88350599;
export const DEBUG_SPECTATE_FORMAT_VERSION = 2;
export const DEBUG_SPECTATE_SAMPLE_MS = 50;
export const DEBUG_SPECTATE_MAX_FRAMES = 12_001;
export const DEBUG_SPECTATE_MAX_SESSIONS = 16;
export const DEBUG_SPECTATE_TTL_MS = 15 * 60_000;
export const debugSpectateContextId = (id: string): string => `debug-spectate:${id}`;
