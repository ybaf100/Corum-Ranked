export type DomainErrorCode =
  | "INVALID_CONFIG"
  | "INVALID_MAP"
  | "CONFLICTING_CANONICAL_MAP"
  | "CONFLICTING_MAP_ALIAS"
  | "INSUFFICIENT_POOL_MAPS"
  | "INVALID_BAN"
  | "INVALID_ATTEMPT"
  | "ATTEMPT_ALREADY_ACTIVE"
  | "ATTEMPT_START_WINDOW_CLOSED"
  | "ROUND_ALREADY_FINISHED"
  | "INVALID_ROUND_TRANSITION"
  | "INVALID_MATCH_TRANSITION"
  | "INVALID_DEATHMATCH"
  | "PROFILE_ALREADY_SEEDED";

export class RankedDomainError extends Error {
  public readonly code: DomainErrorCode;
  public readonly details: Readonly<Record<string, unknown>>;

  public constructor(
    code: DomainErrorCode,
    message: string,
    details: Readonly<Record<string, unknown>> = {},
  ) {
    super(message);
    this.name = "RankedDomainError";
    this.code = code;
    this.details = details;
  }
}
