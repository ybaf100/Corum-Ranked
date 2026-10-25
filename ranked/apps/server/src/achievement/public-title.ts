import {achievementDefinition,achievementScopeLabel} from "@corum-ranked/rules";
export function publicAchievementTitle(achievementId:string|null,level:number|null){
    if (!achievementId || level === null) return null;
    const definition = achievementDefinition(achievementId);
    if (!definition) return null;
    if (definition.type === "ONE_SHOT") {
      if (level !== 0) return null;
      return {
        achievementId: definition.id,
        scope: definition.scope,
        level: null,
        displayLabel: null,
        nameKo: `[${achievementScopeLabel(definition.scope, "ko")}] ${definition.nameKo}`,
        nameEn: `[${achievementScopeLabel(definition.scope, "en")}] ${definition.nameEn}`,
        displayColor: null,
        rarity: definition.rarity,
      };
    }
    const stage = definition.levels[level - 1];
    if (!stage) return null;
    return {
      achievementId: definition.id,
      scope: definition.scope,
      level: stage.level,
      displayLabel: stage.displayLabel,
      nameKo: `[${achievementScopeLabel(definition.scope, "ko")}] ${definition.nameKo}`,
      nameEn: `[${achievementScopeLabel(definition.scope, "en")}] ${definition.nameEn}`,
      displayColor: stage.displayColor,
      rarity: null,
    };
  }
