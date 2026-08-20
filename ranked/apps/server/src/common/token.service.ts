import { createHmac, randomBytes, timingSafeEqual } from "node:crypto";
import { Inject, Injectable } from "@nestjs/common";
import {
  SERVER_ENVIRONMENT,
  type ServerEnvironment,
} from "../config/server-environment.js";

@Injectable()
export class TokenService {
  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
  ) {}

  public createSessionToken(): string {
    return `crs_${randomBytes(32).toString("base64url")}`;
  }

  public deriveMatchToken(matchId: string, playerId: string, sessionId: string): string {
    const signature = createHmac("sha256", this.environment.sessionTokenSecret)
      .update(`match:${matchId}:${playerId}:${sessionId}`)
      .digest("base64url");
    return `crm_${signature}`;
  }

  public hash(token: string): string {
    return createHmac("sha256", this.environment.sessionTokenSecret)
      .update(token)
      .digest("hex");
  }

  public matches(token: string, expectedHash: string): boolean {
    const actual = Buffer.from(this.hash(token), "hex");
    const expected = Buffer.from(expectedHash, "hex");
    return actual.length === expected.length && timingSafeEqual(actual, expected);
  }
}
