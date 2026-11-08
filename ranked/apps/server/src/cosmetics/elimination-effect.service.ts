import { ForbiddenException, Inject, Injectable, NotFoundException } from "@nestjs/common";
import {
  ELIMINATION_EFFECTS, ELIMINATION_REQUIREMENTS, achievementApGrade, eliminationEffectUnlocked, highestBaseTier,
  normalizeEliminationEffect, type EliminationEffect, type EliminationUnlockFacts,
} from "@corum-ranked/rules";
import { DATABASE, type DatabasePort, type SqlExecutor } from "../database/database.port.js";

interface FactsRow {
  total_ap: number | string;
  elimination_effect: string;
  solo_tier: string | null;
  survival_tier: string | null;
  solo_gold_reached: boolean | null;
  survival_gold_reached: boolean | null;
  survival_peak_ledger: string | null;
}

/** Survival elimination effects: catalog, unlock state and the equipped choice.
 * Unlocks follow peak tiers (Survival ledger history, Solo GOLD flag) and the
 * claimed AP grade, so once reached they stay unlocked. */
@Injectable()
export class EliminationEffectService {
  public constructor(@Inject(DATABASE) private readonly database: DatabasePort) {}

  public async state(playerId: string) {
    return this.view(await this.facts(this.database, playerId));
  }

  public async equip(playerId: string, requested: string) {
    const effect = normalizeEliminationEffect(requested);
    if (effect !== requested) throw new NotFoundException("Unknown elimination effect");
    return this.database.transaction(async (tx) => {
      const facts = await this.facts(tx, playerId, true);
      if (!eliminationEffectUnlocked(effect, facts.unlock)) throw new ForbiddenException("Elimination effect is locked");
      await tx.query("UPDATE ranked_profiles SET elimination_effect=$2 WHERE player_id=$1", [playerId, effect]);
      return this.view({ ...facts, equipped: effect });
    });
  }

  /** The equipped effects for match admission; locked or unknown values fall back to DEFAULT. */
  public static async equippedWithin(tx: SqlExecutor, playerIds: readonly string[]): Promise<Map<string, EliminationEffect>> {
    if (!playerIds.length) return new Map();
    const rows = await tx.query<{ player_id: string; elimination_effect: string }>(
      "SELECT player_id, elimination_effect FROM ranked_profiles WHERE player_id = ANY($1::uuid[])", [playerIds]);
    return new Map(rows.rows.map((row) => [row.player_id, normalizeEliminationEffect(row.elimination_effect)]));
  }

  private async facts(executor: SqlExecutor, playerId: string, lock = false) {
    const row = (await executor.query<FactsRow>(
      `SELECT rp.total_ap, rp.elimination_effect, rp.displayed_tier AS solo_tier, sp.displayed_tier AS survival_tier,
              m.solo_gold_reached, m.survival_gold_reached,
              (SELECT string_agg(DISTINCT l.tier_after, ',') FROM sur_lp_ledger l WHERE l.player_id = rp.player_id) AS survival_peak_ledger
       FROM ranked_profiles rp
       LEFT JOIN sur_profiles sp ON sp.player_id = rp.player_id
       LEFT JOIN ranked_achievement_metrics m ON m.player_id = rp.player_id
       WHERE rp.player_id = $1${lock ? " FOR UPDATE OF rp" : ""}`, [playerId])).rows[0];
    if (!row) throw new NotFoundException("Ranked profile is missing");
    const unlock: EliminationUnlockFacts = {
      survivalPeak: highestBaseTier([row.survival_tier, ...(row.survival_peak_ledger ?? "").split(","),
        row.survival_gold_reached ? "GOLD" : null]),
      soloPeak: highestBaseTier([row.solo_tier, row.solo_gold_reached ? "GOLD" : null]),
      totalAp: Number(row.total_ap) || 0,
    };
    return { unlock, equipped: normalizeEliminationEffect(row.elimination_effect) };
  }

  private view(facts: { unlock: EliminationUnlockFacts; equipped: EliminationEffect }) {
    const unlocked = (effect: EliminationEffect) => eliminationEffectUnlocked(effect, facts.unlock);
    // A stored choice that is no longer unlocked plays as DEFAULT.
    const equipped = unlocked(facts.equipped) ? facts.equipped : "DEFAULT";
    return {
      equipped,
      survivalPeak: facts.unlock.survivalPeak,
      soloPeak: facts.unlock.soloPeak,
      grade: achievementApGrade(facts.unlock.totalAp),
      effects: ELIMINATION_EFFECTS.map((id) => ({ id, unlocked: unlocked(id), requirement: ELIMINATION_REQUIREMENTS[id] })),
    };
  }
}
