// Lobby, queue and gameplay share a 90s admission lease, tolerating temporary
// network stalls. Explicit Close releases immediately; a crashed process loses
// admission after 90s without a heartbeat. Token TTL remains separate.
export const RANKED_SESSION_LEASE_SECONDS = 90;
export const RANKED_SESSION_HEARTBEAT_SECONDS = 5;
export const sessionAliveSince = (now: Date): string =>
  new Date(now.getTime() - RANKED_SESSION_LEASE_SECONDS * 1_000).toISOString();
