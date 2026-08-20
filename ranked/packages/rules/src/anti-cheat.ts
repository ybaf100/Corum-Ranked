export interface InstalledModSnapshot {
  readonly id: string;
  readonly version: string;
  readonly enabled: boolean;
  readonly loaded: boolean;
  readonly internal: boolean;
  readonly system: boolean;
  readonly settings?: Readonly<Record<string, boolean | number | string>>;
}

export interface AllowedModRule {
  readonly id: string;
  readonly displayName: string;
  readonly minVersion?: string;
  readonly maxVersion?: string;
  readonly required: boolean;
  readonly enabled: boolean;
}

export interface ClientEnvironmentPolicy {
  readonly allowedMods: readonly AllowedModRule[];
  readonly cbf: {
    readonly modId: string;
    readonly requiredSettings: Readonly<Record<string, boolean | number | string>>;
  };
}

export interface ClientEnvironmentDecision {
  readonly allowed: boolean;
  readonly unauthorizedModIds: readonly string[];
  readonly allowedModIds: readonly string[];
  readonly missingRequiredModIds: readonly string[];
  readonly versionViolations: readonly string[];
  readonly cbfIssues: readonly string[];
}

interface ParsedVersion {
  readonly numbers: readonly number[];
  readonly prerelease: readonly (number | string)[];
}

const parseVersion = (version: string): ParsedVersion | null => {
  const match = version.trim().replace(/^v/i, "").match(/^(\d+(?:\.\d+)*)(?:-([0-9A-Za-z.-]+))?/);
  if (!match?.[1]) return null;
  return {
    numbers: match[1].split(".").map(Number),
    prerelease: match[2]
      ? match[2].split(".").map((part) => (/^\d+$/.test(part) ? Number(part) : part))
      : [],
  };
};

const compareVersions = (leftText: string, rightText: string): number | null => {
  const left = parseVersion(leftText);
  const right = parseVersion(rightText);
  if (!left || !right) return null;
  const numericLength = Math.max(left.numbers.length, right.numbers.length);
  for (let index = 0; index < numericLength; index += 1) {
    const leftPart = left.numbers[index] ?? 0;
    const rightPart = right.numbers[index] ?? 0;
    if (leftPart !== rightPart) return leftPart > rightPart ? 1 : -1;
  }
  if (left.prerelease.length === 0 && right.prerelease.length > 0) return 1;
  if (left.prerelease.length > 0 && right.prerelease.length === 0) return -1;
  const prereleaseLength = Math.max(left.prerelease.length, right.prerelease.length);
  for (let index = 0; index < prereleaseLength; index += 1) {
    const leftPart = left.prerelease[index];
    const rightPart = right.prerelease[index];
    if (leftPart === undefined) return -1;
    if (rightPart === undefined) return 1;
    if (leftPart === rightPart) continue;
    if (typeof leftPart === "number" && typeof rightPart === "string") return -1;
    if (typeof leftPart === "string" && typeof rightPart === "number") return 1;
    return leftPart > rightPart ? 1 : -1;
  }
  return 0;
};

export const evaluateClientEnvironment = (
  installedMods: readonly InstalledModSnapshot[],
  policy: ClientEnvironmentPolicy,
): ClientEnvironmentDecision => {
  const rules = policy.allowedMods.filter((rule) => rule.enabled);
  const allowedById = new Map(rules.map((rule) => [rule.id, rule]));
  const userMods = installedMods.filter((mod) => !mod.internal && !mod.system);
  const installedById = new Map(userMods.map((mod) => [mod.id, mod]));
  const unauthorizedModIds = userMods
    .filter((mod) => !allowedById.has(mod.id))
    .map((mod) => mod.id)
    .sort();
  const missingRequiredModIds = rules
    .filter((rule) => rule.required && !installedById.has(rule.id))
    .map((rule) => rule.id)
    .sort();
  const versionViolations: string[] = [];

  for (const mod of userMods) {
    const rule = allowedById.get(mod.id);
    if (!rule) continue;
    if (rule.minVersion) {
      const comparison = compareVersions(mod.version, rule.minVersion);
      if (comparison === null || comparison < 0) {
        versionViolations.push(`${mod.id}: installed ${mod.version}, minimum ${rule.minVersion}`);
      }
    }
    if (rule.maxVersion) {
      const comparison = compareVersions(mod.version, rule.maxVersion);
      if (comparison === null || comparison > 0) {
        versionViolations.push(`${mod.id}: installed ${mod.version}, maximum ${rule.maxVersion}`);
      }
    }
  }

  const cbfIssues: string[] = [];
  const cbf = installedById.get(policy.cbf.modId);
  if (!cbf) {
    cbfIssues.push("CBF_NOT_INSTALLED");
  } else {
    if (!cbf.enabled || !cbf.loaded) cbfIssues.push("CBF_NOT_ACTIVE");
    for (const [setting, requiredValue] of Object.entries(policy.cbf.requiredSettings)) {
      if (cbf.settings?.[setting] !== requiredValue) {
        cbfIssues.push(`CBF_SETTING_MISMATCH:${setting}`);
      }
    }
  }

  return {
    allowed:
      unauthorizedModIds.length === 0 &&
      missingRequiredModIds.length === 0 &&
      versionViolations.length === 0 &&
      cbfIssues.length === 0,
    unauthorizedModIds,
    allowedModIds: rules.map((rule) => rule.id).sort(),
    missingRequiredModIds,
    versionViolations,
    cbfIssues,
  };
};
