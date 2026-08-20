import {
  RANKED_TIERS,
  SeededRandom,
  canonicalActiveMaps,
  selectCandidateMaps,
  validateOperationalConfig,
} from "@corum-ranked/rules";
import type { RankedConfigDocument } from "./ranked-config.document.js";

export const validateRankedConfigDocument = (document: RankedConfigDocument): readonly string[] => {
  const errors = [...validateOperationalConfig(document.operational).errors];
  if (!document.generation.trim()) errors.push("document.generation is required");
  if (document.operational.generation !== document.generation) {
    errors.push("document and operational generations must match");
  }
  if (!Number.isFinite(Date.parse(document.generatedAt))) {
    errors.push("document.generatedAt must be an ISO timestamp");
  }

  try {
    canonicalActiveMaps(document.maps);
    for (const [index, tier] of RANKED_TIERS.entries()) {
      selectCandidateMaps(tier, document.maps, new SeededRandom(index + 1));
    }
  } catch (error) {
    errors.push(error instanceof Error ? error.message : "Ranked map pool is invalid");
  }

  const enabledRules = document.allowedMods.filter((rule) => rule.enabled);
  const duplicateIds = enabledRules
    .map((rule) => rule.id)
    .filter((id, index, ids) => ids.indexOf(id) !== index);
  if (duplicateIds.length > 0) errors.push(`allowedMods contains duplicate IDs: ${duplicateIds.join(", ")}`);

  const requiredIds = new Set(
    enabledRules.filter((rule) => rule.required).map((rule) => rule.id),
  );
  if (!requiredIds.has(document.operational.cbf.modId)) {
    errors.push("the configured CBF mod must be enabled and required in allowedMods");
  }
  if (!requiredIds.has("hwanhee1.corum_ranked")) {
    errors.push("hwanhee1.corum_ranked must be enabled and required in allowedMods");
  }
  return errors;
};
