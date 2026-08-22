import {
  RANKED_TIERS,
  SeededRandom,
  canonicalActiveMaps,
  selectCandidateMaps,
  validateOperationalConfig,
} from "@corum-ranked/rules";
import type { RankedConfigDocument } from "./ranked-config.document.js";


const validateClientPresentation = (document: RankedConfigDocument): string[] => {
  const client = document.client;
  if (!client) return [];
  const errors: string[] = [];
  const { audio, ui } = client;
  if (!audio || !ui) return ["client.audio and client.ui are required when client is present"];

  if (!Number.isFinite(audio.fadeInSeconds) || audio.fadeInSeconds < 0 || audio.fadeInSeconds > 10) {
    errors.push("client.audio.fadeInSeconds must be between 0 and 10");
  }
  if (!Number.isFinite(audio.fadeOutSeconds) || audio.fadeOutSeconds < 0 || audio.fadeOutSeconds > 10) {
    errors.push("client.audio.fadeOutSeconds must be between 0 and 10");
  }
  if (!Number.isFinite(ui.fadeInSeconds) || ui.fadeInSeconds < 0 || ui.fadeInSeconds > 3) {
    errors.push("client.ui.fadeInSeconds must be between 0 and 3");
  }
  if (!Number.isFinite(ui.fadeOutSeconds) || ui.fadeOutSeconds < 0 || ui.fadeOutSeconds > 3) {
    errors.push("client.ui.fadeOutSeconds must be between 0 and 3");
  }

  const keys = new Set<string>();
  for (const resource of audio.resources ?? []) {
    if (!resource.key?.trim()) errors.push("client.audio resource key is required");
    if (keys.has(resource.key)) errors.push(`client.audio contains duplicate resource key: ${resource.key}`);
    keys.add(resource.key);
    if (!resource.label?.trim()) errors.push(`client.audio resource ${resource.key || "<unknown>"} label is required`);
    if (!Number.isInteger(resource.songId) || resource.songId <= 0) {
      errors.push(`client.audio resource ${resource.key || "<unknown>"} songId must be a positive integer`);
    }
    if (!Number.isFinite(resource.startSeconds) || resource.startSeconds < 0) {
      errors.push(`client.audio resource ${resource.key || "<unknown>"} startSeconds must be >= 0`);
    }
  }
  if (audio.enabled && (audio.resources?.length ?? 0) === 0) {
    errors.push("client.audio.resources must contain at least one resource when audio is enabled");
  }
  return errors;
};

export const validateRankedConfigDocument = (document: RankedConfigDocument): readonly string[] => {
  const errors = [...validateOperationalConfig(document.operational).errors, ...validateClientPresentation(document)];
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
