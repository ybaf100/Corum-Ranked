// Lobby, queue and gameplay share one short-lived admission lease. Token TTL
// remains separate; stopped heartbeats cannot hold an account for hours.
export const RANKED_SESSION_LEASE_SECONDS = 20;
export const RANKED_SESSION_HEARTBEAT_SECONDS = 5;
export const sessionAliveSince = (now: Date): string =>
  new Date(now.getTime() - RANKED_SESSION_LEASE_SECONDS * 1_000).toISOString();
