import { BadRequestException, ConflictException, ForbiddenException, Inject, Injectable, NotFoundException } from "@nestjs/common";
import { createHash, timingSafeEqual } from "node:crypto";
import { canonicalActiveMaps, RankedDomainError, resolveFixedMatchMaps, snapshotMap, type FixedMapRequest } from "@corum-ranked/rules";
import { ID_GENERATOR, SERVER_CLOCK, type IdGenerator, type ServerClock } from "../common/runtime.module.js";
import { RankedConfigService } from "../config/ranked-config.service.js";
import { SERVER_ENVIRONMENT, type ServerEnvironment } from "../config/server-environment.js";
import { DATABASE, type DatabasePort } from "../database/database.port.js";
import { QueueService } from "../queue/queue.service.js";
import { SessionService } from "../session/session.service.js";
import { sessionAliveSince } from "../session/session-presence.policy.js";
import type { EnvironmentRecheckDto } from "../session/session.dto.js";
import type { RankedSessionContext } from "../session/session.types.js";

/** A pending invite lives this long; the target answers from the lobby. */
export const DEBUG_CHALLENGE_SECONDS = 60;

export type DebugChallengeStatus = "PENDING" | "ACCEPTED" | "DECLINED" | "CANCELLED" | "EXPIRED";
interface DebugChallenge {
  readonly id: string;
  readonly challenger: RankedSessionContext;
  readonly targetPlayerId: string;
  readonly targetName: string;
  readonly fixedMaps: FixedMapRequest;
  /** "R1 Title", ..., "DM Title" ("random" for an empty slot). */
  readonly maps: readonly { readonly slot: string; readonly title: string | null }[];
  readonly createdAt: Date;
  readonly expiresAt: Date;
  status: DebugChallengeStatus;
  matchId?: string;
  challengerMatchToken?: string;
  settledAt?: Date;
}

/**
 * Debug-client lobby challenges: a debug player sends designated maps to
 * another debug player who is connected and idle in the lobby; accepting
 * starts a normal scoring Ranked match on those maps (no ban phase).
 * Pending invites are process memory (the single server); a restart drops them.
 */
@Injectable()
export class DebugChallengeService {
  private readonly challenges = new Map<string, DebugChallenge>();

  public constructor(
    @Inject(SERVER_ENVIRONMENT) private readonly environment: ServerEnvironment,
    @Inject(DATABASE) private readonly database: DatabasePort,
    @Inject(SERVER_CLOCK) private readonly clock: ServerClock,
    @Inject(ID_GENERATOR) private readonly ids: IdGenerator,
    private readonly config: RankedConfigService,
    private readonly sessions: SessionService,
    private readonly queue: QueueService,
  ) {}

  /** Debug players connected and idle in the lobby (not queued or in a match). */
  public async lobby(session: RankedSessionContext) {
    this.requireDebug(session);
    const now = this.clock.now();
    const rows = (await this.database.query<{ player_id: string; gd_username: string; displayed_tier: string; visible_ranked_score: number | null }>(
      `SELECT DISTINCT ON (p.id) p.id AS player_id, p.gd_username, pr.displayed_tier, pr.visible_ranked_score
       FROM ranked_sessions s
       JOIN ranked_players p ON p.id = s.player_id
       JOIN ranked_profiles pr ON pr.player_id = p.id
       WHERE s.revoked_at IS NULL AND s.expires_at > $1 AND s.last_heartbeat_at > $2
         AND s.environment_snapshot->>'clientBuildChannel' = 'debug'
         AND NOT p.is_bot AND p.id <> $3
         AND NOT EXISTS (SELECT 1 FROM ranked_matches m WHERE (m.player_a_id = p.id OR m.player_b_id = p.id)
                         AND m.state NOT IN ('MATCH_RESULT', 'CANCELLED'))
         AND NOT EXISTS (SELECT 1 FROM ranked_queue_entries q WHERE q.player_id = p.id AND q.status = 'QUEUED')
       ORDER BY p.id, s.last_heartbeat_at DESC
       LIMIT 50`,
      [now.toISOString(), sessionAliveSince(now), session.playerId],
    )).rows;
    return {
      players: rows.map((row) => ({ playerId: row.player_id, name: row.gd_username, tier: row.displayed_tier, lp: row.visible_ranked_score }))
        .sort((a, b) => a.name.localeCompare(b.name)),
      serverNow: now.toISOString(),
    };
  }

  public async send(session: RankedSessionContext, body: EnvironmentRecheckDto & { password: string; targetPlayerId: string; fixedMaps: FixedMapRequest }) {
    this.requireDebug(session);
    this.requirePassword(body.password);
    this.sessions.assertEnvironment(body.installedMods);
    if (body.targetPlayerId === session.playerId) throw new BadRequestException("Choose another player");
    this.prune();
    const now = this.clock.now();
    for (const challenge of this.challenges.values())
      if (challenge.status === "PENDING" && challenge.challenger.playerId === session.playerId)
        throw new ConflictException("Cancel your pending challenge first");
    const target = (await this.lobby(session)).players.find((player) => player.playerId === body.targetPlayerId);
    if (!target) throw new NotFoundException("That player is not idle in the lobby");
    const maps = this.describe(body.fixedMaps);
    const challenge: DebugChallenge = {
      id: this.ids.next(), challenger: session, targetPlayerId: target.playerId, targetName: target.name,
      fixedMaps: body.fixedMaps, maps, createdAt: now,
      expiresAt: new Date(now.getTime() + DEBUG_CHALLENGE_SECONDS * 1_000), status: "PENDING",
    };
    this.challenges.set(challenge.id, challenge);
    return { challenge: this.view(challenge, session), serverNow: now.toISOString() };
  }

  /** The caller's incoming invites and its own latest challenge (with its
   * match assignment once accepted). */
  public state(session: RankedSessionContext) {
    this.requireDebug(session);
    this.prune();
    const incoming = [...this.challenges.values()]
      .filter((challenge) => challenge.status === "PENDING" && challenge.targetPlayerId === session.playerId)
      .map((challenge) => this.view(challenge, session));
    const outgoing = [...this.challenges.values()]
      .filter((challenge) => challenge.challenger.playerId === session.playerId)
      .sort((a, b) => b.createdAt.getTime() - a.createdAt.getTime())[0];
    return { incoming, outgoing: outgoing ? this.view(outgoing, session) : null, serverNow: this.clock.now().toISOString() };
  }

  public async accept(session: RankedSessionContext, id: string, body: EnvironmentRecheckDto) {
    this.requireDebug(session);
    const challenge = this.pending(id);
    if (challenge.targetPlayerId !== session.playerId) throw new ForbiddenException("Not your challenge");
    const created = await this.queue.createChallengeMatch(challenge.challenger, session, body, challenge.fixedMaps);
    challenge.status = "ACCEPTED"; challenge.settledAt = this.clock.now();
    challenge.matchId = created.matchId; challenge.challengerMatchToken = created.challengerMatchToken;
    return { matchId: created.matchId, matchToken: created.targetMatchToken, side: "B", serverNow: this.clock.now().toISOString() };
  }

  public decline(session: RankedSessionContext, id: string) {
    this.requireDebug(session);
    const challenge = this.pending(id);
    if (challenge.targetPlayerId !== session.playerId) throw new ForbiddenException("Not your challenge");
    challenge.status = "DECLINED"; challenge.settledAt = this.clock.now();
    return { challenge: this.view(challenge, session) };
  }

  public cancel(session: RankedSessionContext, id: string) {
    this.requireDebug(session);
    const challenge = this.pending(id);
    if (challenge.challenger.playerId !== session.playerId) throw new ForbiddenException("Not your challenge");
    challenge.status = "CANCELLED"; challenge.settledAt = this.clock.now();
    return { challenge: this.view(challenge, session) };
  }

  private view(challenge: DebugChallenge, session: RankedSessionContext) {
    const mine = challenge.challenger.playerId === session.playerId && challenge.challenger.sessionId === session.sessionId;
    return {
      id: challenge.id, status: challenge.status,
      from: challenge.challenger.gdUsername, to: challenge.targetName,
      maps: challenge.maps, scoring: true,
      expiresAt: challenge.expiresAt.toISOString(),
      // The challenger's own session receives its match assignment.
      ...(mine && challenge.matchId ? { matchId: challenge.matchId, matchToken: challenge.challengerMatchToken, side: "A" } : {}),
    };
  }

  private describe(request: FixedMapRequest) {
    const config = this.config.getSnapshot();
    const pool = canonicalActiveMaps(config.maps).map(snapshotMap);
    try {
      // Every chosen id must be an active map; empty slots stay random.
      const resolved = resolveFixedMatchMaps(request, config.maps, pool, request.rounds.length);
      return [
        ...request.rounds.map((id, index) => ({ slot: `R${index + 1}`, title: id ? resolved.rounds[index]!.title : null })),
        { slot: "DM", title: resolved.deathmatch?.title ?? null },
      ];
    } catch (error) {
      if (error instanceof RankedDomainError) throw new BadRequestException(error.message);
      throw error;
    }
  }

  private pending(id: string): DebugChallenge {
    this.prune();
    const challenge = this.challenges.get(id);
    if (!challenge) throw new NotFoundException("Challenge not found");
    if (challenge.status !== "PENDING") throw new ConflictException(`Challenge is ${challenge.status.toLowerCase()}`);
    return challenge;
  }

  /** Pending invites expire; settled ones are kept a minute for the sender's poll. */
  private prune() {
    const now = this.clock.now().getTime();
    for (const [id, challenge] of this.challenges) {
      if (challenge.status === "PENDING" && challenge.expiresAt.getTime() <= now) { challenge.status = "EXPIRED"; challenge.settledAt = new Date(now); }
      if (challenge.status !== "PENDING" && (challenge.settledAt?.getTime() ?? now) + 60_000 <= now) this.challenges.delete(id);
    }
  }

  private requireDebug(session: RankedSessionContext) {
    if (!this.environment.debugBot) throw new NotFoundException("Debug challenges are disabled");
    if (session.clientBuildChannel !== "debug") throw new ForbiddenException("Debug challenges require the debug client");
  }

  private requirePassword(password: string) {
    const expected = this.environment.debugBot?.password ?? "";
    const digest = (value: string) => createHash("sha256").update(value).digest();
    if (!expected || !timingSafeEqual(digest(password ?? ""), digest(expected))) throw new NotFoundException("Incorrect password.");
  }
}
