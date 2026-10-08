/**
 * Corum Ranked 운영 설정 전용 Apps Script 모듈.
 * 실시간 경기/attempt 데이터는 절대 Spreadsheet에 저장하지 않는다.
 * 기존 Corum Integration setup/doPost 흐름과 분리되어 있다.
 */
var CORUM_RANKED_SHEETS = Object.freeze({
  tiers: "Ranked Tiers",
  seeds: "Ranked CSMP Seed",
  allowedMods: "Ranked Allowed Mods",
  config: "Ranked Config",
});

// Ranked owns a separate map catalog. Corum Integration continues to use
// MAPS_SHEET_NAME / sheet1 through Code.gs, while ranked_config reads sheet2
// unless the operator explicitly sets RANKED_MAPS_SHEET_NAME.
var CORUM_RANKED_DEFAULT_MAPS_SHEET = "sheet2";

var CORUM_RANKED_MAP_HEADERS = Object.freeze([
  "Ranked Pool (1~6)",
  "Qualifying %",
]);

var CORUM_RANKED_TIER_HEADERS = Object.freeze([
  "Tier",
  "최소 점수(포함)",
  "최대 점수(미포함)",
  "Main Pool",
  "Deathmatch Pool",
  "Promotion Games",
  "Promotion Wins Required",
]);

var CORUM_RANKED_SEED_HEADERS = Object.freeze([
  "CSMP Tier",
  "Initial Hidden MMR",
]);

var CORUM_RANKED_ALLOWED_MOD_HEADERS = Object.freeze([
  "Mod ID",
  "표시명",
  "최소 버전",
  "최대 버전",
  "Required",
  "Enabled",
]);

var CORUM_RANKED_CONFIG_HEADERS = Object.freeze([
  "Key",
  "Value",
  "설명",
]);

var CORUM_RANKED_TIER_DEFAULTS = Object.freeze([
  Object.freeze(["RED", "", "", 2, 2, 3, 2]),
  Object.freeze(["AQUA", "", "", 3, 3, 3, 2]),
  Object.freeze(["BRONZE", "", "", 4, 4, 5, 3]),
  Object.freeze(["SILVER", "", "", 5, 5, 5, 3]),
  Object.freeze(["GOLD", "", "", 6, 6, 0, 0]),
]);

var CORUM_RANKED_SEED_DEFAULTS = Object.freeze([
  Object.freeze(["NONE", ""]),
  Object.freeze(["RED", ""]),
  Object.freeze(["AQUA", ""]),
  Object.freeze(["BRONZE", ""]),
  Object.freeze(["SILVER", ""]),
  Object.freeze(["GOLD", ""]),
]);

var CORUM_RANKED_ALLOWED_MOD_DEFAULTS = Object.freeze([
  Object.freeze([
    "hwanhee1.corum_ranked",
    "Corum Ranked",
    "",
    "",
    true,
    true,
  ]),
  Object.freeze([
    "syzzi.click_between_frames",
    "Click Between Frames",
    "",
    "",
    true,
    true,
  ]),
  Object.freeze([
    "geode.node-ids",
    "Node IDs",
    "v1.23.3",
    "",
    true,
    true,
  ]),
  Object.freeze([
    "fleym.nongd",
    "Jukebox",
    "",
    "",
    false,
    true,
  ]),
]);

var CORUM_RANKED_CONFIG_DEFAULTS = Object.freeze([
  Object.freeze(["enabled", false, "모든 필수 운영값 입력·검증 후 TRUE"]),
  Object.freeze(["ruleVersion", "corum-ranked-v0.5", "명세/규칙 버전"]),
  Object.freeze(["roundSeconds", 300, "일반 Round 시간"]),
  Object.freeze(["clearTarget", 3, "LAST ATTEMPT를 발동시키는 Clear 수"]),
  Object.freeze(["finalAttemptWindowSeconds", 10, "5분 이후 attempt 시작 Window"]),
  Object.freeze(["lastAttemptWindowSeconds", 10, "3-Clear LAST ATTEMPT 시작 Window"]),
  Object.freeze(["banSeconds", 10, "동시 비공개 밴 시간"]),
  Object.freeze(["bestOf", 3, "최대 Round 수"]),
  Object.freeze(["mmrAlgorithm", "ELO_V1", "서버가 지원하는 MMR 계산 정책"]),
  Object.freeze(["placementGames", "", "운영 확정 필요"]),
  Object.freeze(["placementKFactor", "", "운영 확정 필요"]),
  Object.freeze(["regularKFactor", "", "운영 확정 필요"]),
  Object.freeze(["expectedScoreDivisor", "", "운영 확정 필요"]),
  Object.freeze(["mmrOpponentStrengthWeight", 1, "상대 MMR 반영 강도: 0=무시, 1=기본, 2=2배"]),
  Object.freeze(["deltaRounding", "", "NEAREST / FLOOR / CEIL 중 운영 확정"]),
  Object.freeze(["visibleScoreBaseChange", 25, "일반 경기 기본 LP 변동"]),
  Object.freeze(["visibleScoreGapDivisor", 400, "MMR-LP 보정 간격"]),
  Object.freeze(["visibleScoreMinFactor", 0.5, "LP 변동 최소 배율"]),
  Object.freeze(["visibleScoreMaxFactor", 1.5, "LP 변동 최대 배율"]),
  Object.freeze(["visibleScoreOpponentStrengthWeight", 1, "상대 MMR의 LP 반영 강도: 0=무시, 1=기본, 2=2배"]),
  Object.freeze(["scoreMarginMaxBonus", 0.35, "스코어 차이 MMR 최대 추가 배율"]),
  Object.freeze(["clearMarginMaxBonus", 0.35, "Clear 차이 MMR 최대 추가 배율"]),
  Object.freeze(["outcomeDisagreementFactor", 0.6, "승패와 스코어/Clear 우세가 엇갈릴 때 MMR 배율"]),
  Object.freeze(["streakStep", 0.05, "연승/연패 1회당 MMR 추가 배율"]),
  Object.freeze(["streakMaxBonus", 0.25, "연승/연패 MMR 최대 추가 배율"]),
  Object.freeze(["promotionScoreBonus", 50, "승급 성공 시 다음 커트라인 추가 LP"]),
  Object.freeze(["promotionFailurePenalty", 50, "승급 실패 시 커트라인 차감 LP"]),
  Object.freeze(["demotionScorePenalty", 50, "강등 확정 시 현재 커트라인 차감 LP"]),
  Object.freeze(["placementStageCount", 5, "싱글플레이 Placement Stage 수"]),
  Object.freeze(["placementMapsPerStage", 2, "Stage당 서로 다른 Map 수"]),
  Object.freeze(["placementRoundSeconds", 180, "Placement Map Main Time (초)"]),
  Object.freeze(["placementFinalAttemptWindowSeconds", 10, "Placement 마지막 Attempt 시작 Window (초)"]),
  Object.freeze(["placementPrepareSeconds", 10, "Placement Map 시작 전 로비 Countdown (초)"]),
  Object.freeze(["placementLevelRevealRemainingSeconds", 5, "Placement 실제 Level 화면 공개 시점 (남은 초)"]),
  Object.freeze(["placementGradeSMin", 425, "S Grade 최소 Map/Stage Score (0~500)"]),
  Object.freeze(["placementGradeAMin", 350, "A Grade 최소 Map/Stage Score (0~500)"]),
  Object.freeze(["placementGradeBMin", 250, "B Grade 최소 Map/Stage Score (0~500)"]),
  Object.freeze(["placementGradeCMin", 150, "C Grade 최소 Map/Stage Score (0~500)"]),
  Object.freeze(["placementTransitionGuardSeconds", 0.3, "Grade animation 중복 방지 시간"]),
  Object.freeze(["placementTopAttemptCount", 2, "Map Score에 합산할 상위 Attempt 수"]),
  Object.freeze(["placementClearTarget", 2, "Map 즉시 종료 및 S 확정 Clear 수"]),
  Object.freeze(["placementPool1Anchor", 0, "Pool 1 Placement Rating anchor"]),
  Object.freeze(["placementPool2Anchor", 1000, "Pool 2 Placement Rating anchor"]),
  Object.freeze(["placementPool3Anchor", 2000, "Pool 3 Placement Rating anchor"]),
  Object.freeze(["placementPool4Anchor", 3000, "Pool 4 Placement Rating anchor"]),
  Object.freeze(["placementPool5Anchor", 4000, "Pool 5 Placement Rating anchor"]),
  Object.freeze(["placementPool6Anchor", 5000, "Pool 6 Placement Rating anchor"]),
  Object.freeze(["placementMapScoreCenter", 300, "Pool anchor 기준 Map Score"]),
  Object.freeze(["placementRatingPerMapScorePoint", 2.5, "Map Score 1점당 Rating 변화"]),
  Object.freeze(["placementMapRatingWeight", 0.6, "10개 Map evidence 비중"]),
  Object.freeze(["placementStageRatingWeight", 0.25, "5개 Stage evidence 비중"]),
  Object.freeze(["placementOverallRatingWeight", 0.15, "전체 Placement evidence 비중"]),
  Object.freeze(["sessionSeconds", "", "서버 세션 유효시간, 운영 확정 필요"]),
  Object.freeze(["readySeconds", "", "운영 확정 필요"]),
  Object.freeze(["reconnectGraceSeconds", "", "운영 확정 필요"]),
  Object.freeze(["queueHeartbeatSeconds", "", "운영 확정 필요"]),
  Object.freeze(["matchHeartbeatSeconds", "", "운영 확정 필요"]),
  Object.freeze(["orphanAttemptSeconds", "", "운영 확정 필요"]),
  Object.freeze(["roundResultSeconds", "", "라운드 결과 표시 시간, 운영 확정 필요"]),
  Object.freeze(["matchmakingInitialRatingRange", "", "초기 MMR 검색 범위"]),
  Object.freeze(["matchmakingWidenPerSecond", "", "대기 1초당 MMR 범위 확장값"]),
  Object.freeze(["matchmakingMaximumRatingRange", "", "최대 MMR 검색 범위"]),
  Object.freeze(["readyTimeoutAction", "", "CANCEL_MATCH / FORFEIT_UNREADY"]),
  Object.freeze(["reconnectTimeoutAction", "", "CANCEL_MATCH / FORFEIT_DISCONNECTED"]),
  Object.freeze(["restartRecoveryAction", "", "CANCEL_MATCH / RESUME"]),

  // Ranked client presentation/audio. Song IDs are Geometry Dash custom-song IDs.
  // Keep rankedAudioEnabled FALSE until at least one valid song ID is configured.
  Object.freeze(["rankedAudioEnabled", false, "Ranked 로비/라운드 대기/결과 BGM 사용"]),
  Object.freeze(["rankedAudioMenuSongId", "", "일반 로비 BGM Geometry Dash Song ID"]),
  Object.freeze(["rankedAudioMenuStartSeconds", 0, "일반 로비 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioDemotionWarningLobbySongId", "", "강등 경고 상태 로비 BGM Song ID (비어 있으면 일반 로비 사용)"]),
  Object.freeze(["rankedAudioDemotionWarningLobbyStartSeconds", 0, "강등 경고 로비 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioPromotionLobbySongId", "", "승급전 상태 로비 BGM Song ID (비어 있으면 일반 로비 사용)"]),
  Object.freeze(["rankedAudioPromotionLobbyStartSeconds", 0, "승급전 로비 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioMatchSongId", "", "1·2라운드 / Match Found / Ban / 준비 대기 BGM Song ID"]),
  Object.freeze(["rankedAudioMatchStartSeconds", 0, "1·2라운드 대기 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioRound3DeathmatchWaitSongId", "", "3라운드 및 Death Match 대기 BGM Song ID (비어 있으면 1·2라운드 BGM 사용)"]),
  Object.freeze(["rankedAudioRound3DeathmatchWaitStartSeconds", 0, "3라운드/Death Match 대기 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioWinSongId", "", "승리 결과 BGM Song ID"]),
  Object.freeze(["rankedAudioWinStartSeconds", 0, "승리 결과 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioLoseSongId", "", "패배 결과 BGM Song ID"]),
  Object.freeze(["rankedAudioLoseStartSeconds", 0, "패배 결과 BGM 시작 지점(초)"]),
  Object.freeze(["rankedAudioFadeInSeconds", 0, "Ranked BGM 즉시 시작(0 권장)"]),
  Object.freeze(["rankedAudioFadeOutSeconds", 0, "Ranked BGM 즉시 전환(0 권장)"]),
  Object.freeze(["rankedUiFadeInSeconds", 0.24, "Ranked UI Fade In 시간"]),
  Object.freeze(["rankedUiFadeOutSeconds", 0.18, "Ranked UI Fade Out 시간"]),

  Object.freeze(["cbfModId", "syzzi.click_between_frames", "필수 CBF Mod ID"]),
  Object.freeze(["cbf.soft-toggle", false, "CBF의 Disable CBF는 꺼짐"]),
  Object.freeze(["cbf.click-on-steps", false, "Click on Steps 모드는 꺼짐"]),
  Object.freeze(["cbf.physics-bypass", false, "물리 변경 방지"]),
]);

/**
 * Ranked 설정 시트만 생성/보수한다. setupCorumIntegration()에서 자동 호출하지 않는다.
 * 기존 운영자가 입력한 값은 덮어쓰지 않고 누락된 기본 행만 추가한다.
 */
function setupCorumRankedConfig() {
  var spreadsheet = getSpreadsheet_();
  var csmpTiersSheet = ensureCsmpTiersSheet_();
  var mapsSheet = ensureRankedMapColumns_();
  var tiersSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_RANKED_SHEETS.tiers,
    CORUM_RANKED_TIER_HEADERS,
  );
  var seedsSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_RANKED_SHEETS.seeds,
    CORUM_RANKED_SEED_HEADERS,
  );
  var allowedModsSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_RANKED_SHEETS.allowedMods,
    CORUM_RANKED_ALLOWED_MOD_HEADERS,
  );
  var configSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_RANKED_SHEETS.config,
    CORUM_RANKED_CONFIG_HEADERS,
  );

  appendMissingRankedRows_(tiersSheet, CORUM_RANKED_TIER_DEFAULTS);
  appendMissingRankedRows_(seedsSheet, CORUM_RANKED_SEED_DEFAULTS);
  appendMissingRankedCsmpSeedRows_(seedsSheet, csmpTiersSheet);
  appendMissingRankedRows_(allowedModsSheet, CORUM_RANKED_ALLOWED_MOD_DEFAULTS);
  appendMissingRankedRows_(configSheet, CORUM_RANKED_CONFIG_DEFAULTS);
  migrateRankedFixedRuleDefaults_(configSheet);
  migrateRankedPlacementBeta9Defaults_(configSheet);
  migrateRankedTierPromotionDefaults_(tiersSheet);

  [mapsSheet, tiersSheet, seedsSheet, allowedModsSheet, configSheet].forEach(function (sheet) {
    sheet.setFrozenRows(1);
  });

  console.log("Corum Ranked 설정 준비 완료 (맵 데이터: " + mapsSheet.getName() + ")");
  console.log("미확정 Seed/MMR/timeout 값을 입력하고 검증한 뒤 enabled를 TRUE로 바꾸세요.");
}

function appendMissingRankedCsmpSeedRows_(seedsSheet, csmpTiersSheet) {
  if (!seedsSheet || !csmpTiersSheet || csmpTiersSheet.getLastRow() < 2) return;
  var values = csmpTiersSheet.getDataRange().getDisplayValues();
  var nameColumn = findOptionalHeaderIndex_(values[0], ["티어명", "티어 이름", "Tier", "Tier Name"]);
  if (nameColumn === -1) return;
  var rows = [];
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var tier = rankedText_(values[rowIndex][nameColumn]).toUpperCase();
    if (tier && tier !== "NONE") rows.push([tier, ""]);
  }
  appendMissingRankedRows_(seedsSheet, rows);
}

function migrateRankedFixedRuleDefaults_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return;
  var values = sheet.getDataRange().getValues();
  var keyColumn = findOptionalHeaderIndex_(values[0], ["Key"]);
  var valueColumn = findOptionalHeaderIndex_(values[0], ["Value"]);
  if (keyColumn === -1 || valueColumn === -1) return;

  var changed = false;
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var key = rankedText_(values[rowIndex][keyColumn]);
    if (key === "ruleVersion" && ["corum-ranked-v0.3", "corum-ranked-v0.4"].indexOf(rankedText_(values[rowIndex][valueColumn])) !== -1) {
      values[rowIndex][valueColumn] = "corum-ranked-v0.5";
      changed = true;
    } else if (key === "roundSeconds" && Number(values[rowIndex][valueColumn]) === 180) {
      values[rowIndex][valueColumn] = 300;
      changed = true;
    } else if (key === "clearTarget" && Number(values[rowIndex][valueColumn]) === 2) {
      values[rowIndex][valueColumn] = 3;
      changed = true;
    } else if (key === "rankedAudioFadeInSeconds" && Number(values[rowIndex][valueColumn]) === 0.8) {
      values[rowIndex][valueColumn] = 0;
      changed = true;
    } else if (key === "rankedAudioFadeOutSeconds" && Number(values[rowIndex][valueColumn]) === 0.6) {
      values[rowIndex][valueColumn] = 0;
      changed = true;
    }
  }
  if (changed) {
    sheet.getRange(1, 1, values.length, values[0].length).setValues(values);
  }
}

function migrateRankedPlacementBeta9Defaults_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return;
  var values = sheet.getDataRange().getValues();
  var keyColumn = findOptionalHeaderIndex_(values[0], ["Key"]);
  var valueColumn = findOptionalHeaderIndex_(values[0], ["Value"]);
  if (keyColumn === -1 || valueColumn === -1) return;
  var oldToNew = {
    placementGradeSMin: [638, 425],
    placementGradeAMin: [525, 350],
    placementGradeBMin: [375, 250],
    placementGradeCMin: [225, 150],
    placementTopAttemptCount: [3, 2],
    placementClearTarget: [3, 2],
    placementMapScoreCenter: [450, 300],
    placementRatingPerMapScorePoint: [1.6666666667, 2.5],
  };
  var changed = false;
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var key = rankedText_(values[rowIndex][keyColumn]);
    var migration = oldToNew[key];
    if (!migration) continue;
    var current = Number(values[rowIndex][valueColumn]);
    if (Math.abs(current - migration[0]) > 0.0000001) continue;
    values[rowIndex][valueColumn] = migration[1];
    changed = true;
  }
  if (changed) sheet.getRange(1, 1, values.length, values[0].length).setValues(values);
}

function migrateRankedTierPromotionDefaults_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return;
  var values = sheet.getDataRange().getValues();
  var tierColumn = findOptionalHeaderIndex_(values[0], ["Tier"]);
  var gamesColumn = findOptionalHeaderIndex_(values[0], ["Promotion Games"]);
  var winsColumn = findOptionalHeaderIndex_(values[0], ["Promotion Wins Required"]);
  if ([tierColumn, gamesColumn, winsColumn].some(function (column) { return column === -1; })) return;
  var defaults = {
    RED: [3, 2], AQUA: [3, 2], BRONZE: [5, 3], SILVER: [5, 3], GOLD: [0, 0],
  };
  var changed = false;
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var tier = rankedText_(values[rowIndex][tierColumn]).toUpperCase();
    if (!defaults[tier]) continue;
    if (rankedText_(values[rowIndex][gamesColumn]) === "") {
      values[rowIndex][gamesColumn] = defaults[tier][0];
      changed = true;
    }
    if (rankedText_(values[rowIndex][winsColumn]) === "") {
      values[rowIndex][winsColumn] = defaults[tier][1];
      changed = true;
    }
  }
  if (changed) sheet.getRange(1, 1, values.length, values[0].length).setValues(values);
}

function appendMissingRankedRows_(sheet, rows) {
  var existingKeys = {};
  if (sheet.getLastRow() >= 2) {
    sheet.getRange(2, 1, sheet.getLastRow() - 1, 1).getDisplayValues().forEach(function (row) {
      var key = String(row[0] || "").trim().toUpperCase();
      if (key) existingKeys[key] = true;
    });
  }
  rows.forEach(function (row) {
    var key = String(row[0] || "").trim().toUpperCase();
    if (!key || existingKeys[key]) return;
    sheet.appendRow(Array.prototype.slice.call(row));
    existingKeys[key] = true;
  });
}

function getCorumRankedConfigResponse_() {
  var document = readCorumRankedConfig_();
  return json_({
    ok: true,
    action: "ranked_config",
    data: document,
  });
}

function readCorumRankedConfig_() {
  var spreadsheet = getSpreadsheet_();
  var rawConfig = readRankedKeyValues_(spreadsheet.getSheetByName(CORUM_RANKED_SHEETS.config));
  var rules = {
    rulesVersion: rankedText_(rawConfig.ruleVersion),
    roundSeconds: rankedNumber_(rawConfig.roundSeconds),
    clearTarget: rankedNumber_(rawConfig.clearTarget),
    finalAttemptWindowSeconds: rankedNumber_(rawConfig.finalAttemptWindowSeconds),
    lastAttemptWindowSeconds: rankedNumber_(rawConfig.lastAttemptWindowSeconds),
    banSeconds: rankedNumber_(rawConfig.banSeconds),
    bestOf: rankedNumber_(rawConfig.bestOf),
  };
  var tierBands = readRankedTierBands_(spreadsheet.getSheetByName(CORUM_RANKED_SHEETS.tiers));
  var seedsSheet = spreadsheet.getSheetByName(CORUM_RANKED_SHEETS.seeds);
  var csmpTiersSheet = spreadsheet.getSheetByName(CORUM_SHEETS.csmpTiers);
  var csmpTierNames = readRankedCsmpTierNames_(csmpTiersSheet);
  var csmpSeeds = readRankedSeeds_(seedsSheet);
  var maps = readRankedMapsFromCorumSheet_();
  var allowedMods = readRankedAllowedMods_(
    spreadsheet.getSheetByName(CORUM_RANKED_SHEETS.allowedMods),
  );
  var operational = {
    enabled: rankedBoolean_(rawConfig.enabled),
    generation: "",
    rules: rules,
    tierBands: tierBands,
    csmpTierNames: csmpTierNames,
    csmpSeeds: csmpSeeds,
    cbf: {
      modId: rankedText_(rawConfig.cbfModId),
      requiredSettings: {
        "soft-toggle": rankedBoolean_(rawConfig["cbf.soft-toggle"]),
        "click-on-steps": rankedBoolean_(rawConfig["cbf.click-on-steps"]),
        "physics-bypass": rankedBoolean_(rawConfig["cbf.physics-bypass"]),
      },
    },
  };

  var mmrPolicy = readRankedMmrPolicy_(rawConfig);
  if (mmrPolicy) operational.mmrPolicy = mmrPolicy;
  var placementPolicy = readRankedPlacementPolicy_(rawConfig);
  if (placementPolicy) operational.placementPolicy = placementPolicy;
  var timeouts = readRankedTimeouts_(rawConfig);
  if (timeouts) operational.timeouts = timeouts;
  var matchmaking = readRankedMatchmaking_(rawConfig);
  if (matchmaking) operational.matchmaking = matchmaking;
  var failurePolicy = readRankedFailurePolicy_(rawConfig);
  if (failurePolicy) operational.failurePolicy = failurePolicy;

  var client = readRankedClientPresentation_(rawConfig);

  var generationInput = {
    rules: rules,
    tierBands: tierBands,
    csmpTierNames: csmpTierNames,
    csmpSeeds: csmpSeeds,
    mmrPolicy: mmrPolicy,
    placementPolicy: placementPolicy,
    timeouts: timeouts,
    matchmaking: matchmaking,
    failurePolicy: failurePolicy,
    cbf: operational.cbf,
    client: client,
    maps: maps,
    allowedMods: allowedMods,
  };
  var generation = "ranked-v0.5-" + rankedStableHash_(JSON.stringify(generationInput));
  operational.generation = generation;
  var validation = validateCorumRankedConfig_(
    operational,
    maps,
    allowedMods,
    client,
    seedsSheet,
    csmpTiersSheet,
  );

  return {
    generation: generation,
    generatedAt: new Date().toISOString(),
    operational: operational,
    client: client,
    maps: maps,
    allowedMods: allowedMods,
    validation: {
      valid: validation.errors.length === 0,
      queueReady: operational.enabled && validation.errors.length === 0,
      errors: validation.errors,
    },
  };
}

function readRankedKeyValues_(sheet) {
  var result = {};
  if (!sheet || sheet.getLastRow() < 2) return result;
  var values = sheet.getDataRange().getValues();
  var keyColumn = findOptionalHeaderIndex_(values[0], ["Key"]);
  var valueColumn = findOptionalHeaderIndex_(values[0], ["Value"]);
  if (keyColumn === -1 || valueColumn === -1) return result;
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var key = String(values[rowIndex][keyColumn] || "").trim();
    if (key) result[key] = values[rowIndex][valueColumn];
  }
  return result;
}

function readRankedTierBands_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return [];
  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var tierColumn = findOptionalHeaderIndex_(header, ["Tier"]);
  var minimumColumn = findOptionalHeaderIndex_(header, ["최소 점수(포함)"]);
  var maximumColumn = findOptionalHeaderIndex_(header, ["최대 점수(미포함)"]);
  var mainPoolColumn = findOptionalHeaderIndex_(header, ["Main Pool"]);
  var deathmatchPoolColumn = findOptionalHeaderIndex_(header, ["Deathmatch Pool"]);
  var promotionGamesColumn = findOptionalHeaderIndex_(header, ["Promotion Games"]);
  var promotionWinsColumn = findOptionalHeaderIndex_(header, ["Promotion Wins Required"]);
  if ([tierColumn, minimumColumn, maximumColumn, mainPoolColumn, deathmatchPoolColumn,
    promotionGamesColumn, promotionWinsColumn].some(function (column) {
    return column === -1;
  })) return [];

  var result = [];
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var tier = rankedText_(row[tierColumn]).toUpperCase();
    if (!tier) continue;
    var minimum = rankedNumber_(row[minimumColumn]);
    var maximumText = rankedText_(row[maximumColumn]);
    var maximum = maximumText === "" ? null : rankedNumber_(row[maximumColumn]);
    var mainPool = rankedNumber_(row[mainPoolColumn]);
    var deathmatchPool = rankedNumber_(row[deathmatchPoolColumn]);
    result.push({
      tier: tier,
      minInclusive: minimum,
      maxExclusive: maximum,
      mainPool: mainPool,
      deathmatchPool: deathmatchPool,
      promotionGames: rankedNumber_(row[promotionGamesColumn]),
      promotionWinsRequired: rankedNumber_(row[promotionWinsColumn]),
    });
  }
  return result;
}

function readRankedSeeds_(sheet) {
  var result = {};
  if (!sheet || sheet.getLastRow() < 2) return result;
  var values = sheet.getDataRange().getValues();
  var tierColumn = findOptionalHeaderIndex_(values[0], ["CSMP Tier"]);
  var seedColumn = findOptionalHeaderIndex_(values[0], ["Initial Hidden MMR"]);
  if (tierColumn === -1 || seedColumn === -1) return result;
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var tier = rankedText_(values[rowIndex][tierColumn]).toUpperCase();
    var seed = rankedNumber_(values[rowIndex][seedColumn]);
    if (tier && seed !== null) result[tier] = seed;
  }
  return result;
}

function readRankedCsmpTierNames_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return [];
  var values = sheet.getDataRange().getDisplayValues();
  var tierColumn = findOptionalHeaderIndex_(
    values[0],
    ["티어명", "티어 이름", "Tier", "Tier Name"],
  );
  if (tierColumn === -1) return [];
  var result = [];
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var tier = rankedText_(values[rowIndex][tierColumn]).toUpperCase();
    if (tier) result.push(tier);
  }
  return result;
}

function getRankedMapsSheet_() {
  var spreadsheet = getSpreadsheet_();
  var configuredName = PropertiesService.getScriptProperties().getProperty(
    "RANKED_MAPS_SHEET_NAME",
  );
  var sheetName = rankedText_(configuredName) || CORUM_RANKED_DEFAULT_MAPS_SHEET;
  var sheet = spreadsheet.getSheetByName(sheetName);
  if (sheet) return sheet;

  throw new Error(
    "Ranked 맵 시트를 찾을 수 없습니다: " + sheetName +
      ". sheet1과 같은 열 구조의 sheet2를 만들거나 RANKED_MAPS_SHEET_NAME을 설정하세요.",
  );
}

function ensureRankedMapColumns_() {
  var sheet = getRankedMapsSheet_();
  var width = Math.max(1, sheet.getLastColumn());
  var headers = sheet.getRange(1, 1, 1, width).getDisplayValues()[0];
  var missing = CORUM_RANKED_MAP_HEADERS.filter(function (header) {
    return findOptionalHeaderIndex_(headers, [header]) === -1;
  });
  if (missing.length > 0) {
    sheet.getRange(1, headers.length + 1, 1, missing.length).setValues([missing]);
  }
  applyRankedMapValidation_(sheet);
  return sheet;
}

function applyRankedMapValidation_(sheet) {
  if (!SpreadsheetApp.newDataValidation || sheet.getLastColumn() < 1) return;
  var headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  var maximumRows = typeof sheet.getMaxRows === "function" ? sheet.getMaxRows() : sheet.getLastRow();
  if (maximumRows < 2) return;
  [
    { aliases: ["Ranked Pool (1~6)"], minimum: 1, maximum: 6 },
    { aliases: ["Qualifying %"], minimum: 0, maximum: 100 },
  ].forEach(function (rule) {
    var column = findOptionalHeaderIndex_(headers, rule.aliases);
    if (column === -1) return;
    var range = sheet.getRange(2, column + 1, maximumRows - 1, 1);
    if (typeof range.setDataValidation !== "function") return;
    var builder = SpreadsheetApp.newDataValidation()
      .requireNumberBetween(rule.minimum, rule.maximum);
    if (typeof builder.setAllowInvalid === "function") builder.setAllowInvalid(false);
    range.setDataValidation(builder.build());
  });
}

function readRankedMapsFromCorumSheet_() {
  var sheet = getRankedMapsSheet_();
  if (!sheet || sheet.getLastRow() < 2) return [];
  var values = sheet.getDataRange().getValues();
  var displayValues = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var columns = {
    canonicalLevelId: findOptionalHeaderIndex_(header, ["맵 코드", "맵코드", "Level ID", "levelId", "ID"]),
    alternateLevelId: findOptionalHeaderIndex_(header, [
      "대체 맵 코드",
      "대체맵코드",
      "Alternate Level ID",
      "Alternative Level ID",
      "Alt Level ID",
      "alternateLevelId",
    ]),
    title: findOptionalHeaderIndex_(header, ["맵 제목", "맵제목", "제목", "Title"]),
    creator: findOptionalHeaderIndex_(header, ["제작자", "Creator"]),
    difficulty: findOptionalHeaderIndex_(header, ["난이도", "Difficulty", "Rating", "레이팅"]),
    length: findOptionalHeaderIndex_(header, ["맵 길이", "맵길이", "길이", "Length"]),
    pool: findOptionalHeaderIndex_(header, ["Ranked Pool (1~6)"]),
    qualifyingPercent: findOptionalHeaderIndex_(header, ["Qualifying %"]),
  };
  if ([columns.canonicalLevelId, columns.title, columns.creator, columns.difficulty, columns.pool,
    columns.qualifyingPercent].some(function (column) { return column === -1; })) return [];

  var result = [];
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var displayRow = displayValues[rowIndex];
    var pool = rankedNumber_(row[columns.pool]);
    if (pool === null) continue;
    var canonicalLevelId = rankedText_(displayRow[columns.canonicalLevelId]);
    if (!/^\d+$/.test(canonicalLevelId) || /^0+$/.test(canonicalLevelId)) continue;
    // Ranked gameplay uses the explicit value in "대체 맵 코드" as the
    // playable Geometry Dash Level ID. Do not call normalizeAlternateLevelId_
    // here because that shared helper intentionally collapses an alternate ID
    // equal to the canonical ID into an empty string.
    var alternateLevelId = columns.alternateLevelId === -1
      ? ""
      : rankedText_(displayRow[columns.alternateLevelId]);
    result.push({
      levelId: alternateLevelId,
      canonicalLevelId: canonicalLevelId,
      alternateLevelId: alternateLevelId || null,
      playableLevelId: alternateLevelId,
      title: rankedText_(row[columns.title]),
      creator: rankedText_(row[columns.creator]),
      difficulty: rankedText_(row[columns.difficulty]),
      length: columns.length === -1 ? "" : rankedText_(row[columns.length]),
      pool: pool,
      qualifyingPercent: rankedPercent_(row[columns.qualifyingPercent], displayRow[columns.qualifyingPercent]),
      active: true,
    });
  }
  return result;
}

function rankedPercent_(rawValue, displayValue) {
  var displayText = rankedText_(displayValue);
  if (/%$/.test(displayText)) {
    var displayed = Number(displayText.replace(/%$/, "").replace(/,/g, "").trim());
    return Number.isFinite(displayed) ? displayed : null;
  }
  return rankedNumber_(rawValue);
}

function readRankedAllowedMods_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return [];
  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var columns = {
    id: findOptionalHeaderIndex_(header, ["Mod ID"]),
    displayName: findOptionalHeaderIndex_(header, ["표시명"]),
    minVersion: findOptionalHeaderIndex_(header, ["최소 버전"]),
    maxVersion: findOptionalHeaderIndex_(header, ["최대 버전"]),
    required: findOptionalHeaderIndex_(header, ["Required"]),
    enabled: findOptionalHeaderIndex_(header, ["Enabled"]),
  };
  if (Object.keys(columns).some(function (key) { return columns[key] === -1; })) return [];

  var result = [];
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var id = rankedText_(row[columns.id]);
    if (!id) continue;
    var rule = {
      id: id,
      displayName: rankedText_(row[columns.displayName]) || id,
      required: rankedBoolean_(row[columns.required]),
      enabled: rankedBoolean_(row[columns.enabled]),
    };
    var minimum = rankedText_(row[columns.minVersion]);
    var maximum = rankedText_(row[columns.maxVersion]);
    if (minimum) rule.minVersion = minimum;
    if (maximum) rule.maxVersion = maximum;
    result.push(rule);
  }
  return result;
}

function readRankedMmrPolicy_(config) {
  var placementGames = rankedNumber_(config.placementGames);
  var placementKFactor = rankedNumber_(config.placementKFactor);
  var regularKFactor = rankedNumber_(config.regularKFactor);
  var expectedScoreDivisor = rankedNumber_(config.expectedScoreDivisor);
  var deltaRounding = rankedText_(config.deltaRounding).toUpperCase();
  var extra = {
    mmrOpponentStrengthWeight: rankedNumber_(config.mmrOpponentStrengthWeight),
    visibleScoreBaseChange: rankedNumber_(config.visibleScoreBaseChange),
    visibleScoreGapDivisor: rankedNumber_(config.visibleScoreGapDivisor),
    visibleScoreMinFactor: rankedNumber_(config.visibleScoreMinFactor),
    visibleScoreMaxFactor: rankedNumber_(config.visibleScoreMaxFactor),
    visibleScoreOpponentStrengthWeight: rankedNumber_(config.visibleScoreOpponentStrengthWeight),
    scoreMarginMaxBonus: rankedNumber_(config.scoreMarginMaxBonus),
    clearMarginMaxBonus: rankedNumber_(config.clearMarginMaxBonus),
    outcomeDisagreementFactor: rankedNumber_(config.outcomeDisagreementFactor),
    streakStep: rankedNumber_(config.streakStep),
    streakMaxBonus: rankedNumber_(config.streakMaxBonus),
    promotionScoreBonus: rankedNumber_(config.promotionScoreBonus),
    promotionFailurePenalty: rankedNumber_(config.promotionFailurePenalty),
    demotionScorePenalty: rankedNumber_(config.demotionScorePenalty),
  };
  if (
    placementGames === null ||
    placementKFactor === null ||
    regularKFactor === null ||
    expectedScoreDivisor === null ||
    !deltaRounding || Object.keys(extra).some(function (key) { return extra[key] === null; })
  ) return null;
  return {
    algorithm: rankedText_(config.mmrAlgorithm).toUpperCase(),
    placementGames: placementGames,
    placementKFactor: placementKFactor,
    regularKFactor: regularKFactor,
    expectedScoreDivisor: expectedScoreDivisor,
    mmrOpponentStrengthWeight: extra.mmrOpponentStrengthWeight,
    deltaRounding: deltaRounding,
    visibleScoreBaseChange: extra.visibleScoreBaseChange,
    visibleScoreGapDivisor: extra.visibleScoreGapDivisor,
    visibleScoreMinFactor: extra.visibleScoreMinFactor,
    visibleScoreMaxFactor: extra.visibleScoreMaxFactor,
    visibleScoreOpponentStrengthWeight: extra.visibleScoreOpponentStrengthWeight,
    scoreMarginMaxBonus: extra.scoreMarginMaxBonus,
    clearMarginMaxBonus: extra.clearMarginMaxBonus,
    outcomeDisagreementFactor: extra.outcomeDisagreementFactor,
    streakStep: extra.streakStep,
    streakMaxBonus: extra.streakMaxBonus,
    promotionScoreBonus: extra.promotionScoreBonus,
    promotionFailurePenalty: extra.promotionFailurePenalty,
    demotionScorePenalty: extra.demotionScorePenalty,
  };
}

function readRankedPlacementPolicy_(config) {
  var policy = {
    stageCount: rankedNumber_(config.placementStageCount),
    mapsPerStage: rankedNumber_(config.placementMapsPerStage),
    roundSeconds: rankedNumber_(config.placementRoundSeconds),
    finalAttemptWindowSeconds: rankedNumber_(config.placementFinalAttemptWindowSeconds),
    prepareSeconds: rankedNumber_(config.placementPrepareSeconds),
    levelRevealRemainingSeconds: rankedNumber_(config.placementLevelRevealRemainingSeconds),
    gradeThresholds: {
      sMin: rankedNumber_(config.placementGradeSMin),
      aMin: rankedNumber_(config.placementGradeAMin),
      bMin: rankedNumber_(config.placementGradeBMin),
      cMin: rankedNumber_(config.placementGradeCMin),
    },
    transitionGuardSeconds: rankedNumber_(config.placementTransitionGuardSeconds),
    topAttemptCount: rankedNumber_(config.placementTopAttemptCount),
    clearTarget: rankedNumber_(config.placementClearTarget),
    poolRatingAnchors: [
      rankedNumber_(config.placementPool1Anchor),
      rankedNumber_(config.placementPool2Anchor),
      rankedNumber_(config.placementPool3Anchor),
      rankedNumber_(config.placementPool4Anchor),
      rankedNumber_(config.placementPool5Anchor),
      rankedNumber_(config.placementPool6Anchor),
    ],
    mapScoreCenter: rankedNumber_(config.placementMapScoreCenter),
    ratingPerMapScorePoint: rankedNumber_(config.placementRatingPerMapScorePoint),
    mapRatingWeight: rankedNumber_(config.placementMapRatingWeight),
    stageRatingWeight: rankedNumber_(config.placementStageRatingWeight),
    overallRatingWeight: rankedNumber_(config.placementOverallRatingWeight),
  };
  var flat = [
    policy.stageCount, policy.mapsPerStage,
    policy.roundSeconds, policy.finalAttemptWindowSeconds,
    policy.prepareSeconds, policy.levelRevealRemainingSeconds,
    policy.gradeThresholds.sMin, policy.gradeThresholds.aMin,
    policy.gradeThresholds.bMin, policy.gradeThresholds.cMin,
    policy.transitionGuardSeconds, policy.topAttemptCount, policy.clearTarget,
    policy.mapScoreCenter, policy.ratingPerMapScorePoint,
    policy.mapRatingWeight, policy.stageRatingWeight, policy.overallRatingWeight,
  ].concat(policy.poolRatingAnchors);
  if (flat.some(function (value) { return value === null; })) return null;
  return policy;
}

function readRankedTimeouts_(config) {
  var result = {
    sessionSeconds: rankedNumber_(config.sessionSeconds),
    readySeconds: rankedNumber_(config.readySeconds),
    reconnectGraceSeconds: rankedNumber_(config.reconnectGraceSeconds),
    queueHeartbeatSeconds: rankedNumber_(config.queueHeartbeatSeconds),
    matchHeartbeatSeconds: rankedNumber_(config.matchHeartbeatSeconds),
    orphanAttemptSeconds: rankedNumber_(config.orphanAttemptSeconds),
    roundResultSeconds: rankedNumber_(config.roundResultSeconds),
  };
  if (Object.keys(result).some(function (key) { return result[key] === null; })) return null;
  return result;
}

function readRankedMatchmaking_(config) {
  var result = {
    initialRatingRange: rankedNumber_(config.matchmakingInitialRatingRange),
    widenPerSecond: rankedNumber_(config.matchmakingWidenPerSecond),
    maximumRatingRange: rankedNumber_(config.matchmakingMaximumRatingRange),
  };
  if (Object.keys(result).some(function (key) { return result[key] === null; })) return null;
  return result;
}

function readRankedFailurePolicy_(config) {
  var ready = rankedText_(config.readyTimeoutAction).toUpperCase();
  var reconnect = rankedText_(config.reconnectTimeoutAction).toUpperCase();
  var restart = rankedText_(config.restartRecoveryAction).toUpperCase();
  if (!ready || !reconnect || !restart) return null;
  return {
    readyTimeoutAction: ready,
    reconnectTimeoutAction: reconnect,
    restartRecoveryAction: restart,
  };
}

function readRankedClientPresentation_(config) {
  var fadeIn = rankedNumber_(config.rankedAudioFadeInSeconds);
  var fadeOut = rankedNumber_(config.rankedAudioFadeOutSeconds);
  var uiFadeIn = rankedNumber_(config.rankedUiFadeInSeconds);
  var uiFadeOut = rankedNumber_(config.rankedUiFadeOutSeconds);

  function resource_(key, label, songIdKey, startKey, loop) {
    var songId = rankedNumber_(config[songIdKey]);
    if (songId === null || songId <= 0) return null;
    var startSeconds = rankedNumber_(config[startKey]);
    if (startSeconds === null || startSeconds < 0) startSeconds = 0;
    return {
      key: key,
      label: label,
      songId: Math.floor(songId),
      startSeconds: startSeconds,
      loop: !!loop,
    };
  }

  var resources = [
    resource_("menu", "Lobby Theme", "rankedAudioMenuSongId", "rankedAudioMenuStartSeconds", true),
    resource_("menu_demotion_warning", "Demotion Warning Lobby Theme", "rankedAudioDemotionWarningLobbySongId", "rankedAudioDemotionWarningLobbyStartSeconds", true),
    resource_("menu_promotion", "Promotion Lobby Theme", "rankedAudioPromotionLobbySongId", "rankedAudioPromotionLobbyStartSeconds", true),
    resource_("match", "Round 1/2 Theme", "rankedAudioMatchSongId", "rankedAudioMatchStartSeconds", true),
    resource_("round3_deathmatch_wait", "Round 3 / Death Match Wait Theme", "rankedAudioRound3DeathmatchWaitSongId", "rankedAudioRound3DeathmatchWaitStartSeconds", true),
    resource_("result_win", "Result Theme", "rankedAudioWinSongId", "rankedAudioWinStartSeconds", false),
    resource_("result_lose", "Result Theme", "rankedAudioLoseSongId", "rankedAudioLoseStartSeconds", false),
  ].filter(function (item) { return item !== null; });

  return {
    audio: {
      enabled: rankedBoolean_(config.rankedAudioEnabled),
      fadeInSeconds: fadeIn === null ? 0 : Math.max(0, fadeIn),
      fadeOutSeconds: fadeOut === null ? 0 : Math.max(0, fadeOut),
      resources: resources,
    },
    ui: {
      fadeInSeconds: uiFadeIn === null ? 0.24 : Math.max(0, uiFadeIn),
      fadeOutSeconds: uiFadeOut === null ? 0.18 : Math.max(0, uiFadeOut),
    },
  };
}

function rankedText_(value) {
  return String(value == null ? "" : value).trim();
}

function rankedNumber_(value) {
  if (value === null || value === undefined || rankedText_(value) === "") return null;
  var numeric = Number(value);
  return Number.isFinite(numeric) ? numeric : null;
}

function rankedBoolean_(value) {
  if (value === true || value === 1) return true;
  var text = rankedText_(value).toLowerCase();
  return text === "true" || text === "yes" || text === "y" || text === "1" || text === "활성";
}

function validateCorumRankedConfig_(
  operational,
  maps,
  allowedMods,
  client,
  seedsSheet,
  csmpTiersSheet
) {
  var errors = [];
  var rules = operational.rules || {};
  if (rules.rulesVersion !== "corum-ranked-v0.5") {
    errors.push("ruleVersion은 corum-ranked-v0.5이어야 합니다.");
  }
  if (rules.roundSeconds !== 300) errors.push("roundSeconds는 300이어야 합니다.");
  if (rules.clearTarget !== 3) errors.push("clearTarget은 3이어야 합니다.");
  if (rules.finalAttemptWindowSeconds !== 10) {
    errors.push("finalAttemptWindowSeconds는 10이어야 합니다.");
  }
  if (rules.lastAttemptWindowSeconds !== 10) {
    errors.push("lastAttemptWindowSeconds는 10이어야 합니다.");
  }
  if (rules.banSeconds !== 10) errors.push("banSeconds는 10이어야 합니다.");
  if (rules.bestOf !== 3) errors.push("bestOf는 3이어야 합니다.");

  validateRankedTiers_(operational.tierBands || [], errors);
  validateRankedCsmpSeeds_(seedsSheet, csmpTiersSheet, errors);

  if (!operational.mmrPolicy) {
    errors.push("MMR/배치 운영값을 모두 입력해야 합니다.");
  } else {
    var mmr = operational.mmrPolicy;
    if (mmr.algorithm !== "ELO_V1") errors.push("지원되는 mmrAlgorithm은 ELO_V1입니다.");
    if (!Number.isInteger(mmr.placementGames) || mmr.placementGames <= 0) {
      errors.push("placementGames는 양의 정수여야 합니다.");
    }
    ["placementKFactor", "regularKFactor", "expectedScoreDivisor"].forEach(function (key) {
      if (!(mmr[key] > 0)) errors.push(key + "는 양수여야 합니다.");
    });
    ["visibleScoreBaseChange", "visibleScoreGapDivisor", "visibleScoreMinFactor", "visibleScoreMaxFactor"].forEach(function (key) {
      if (!(mmr[key] > 0)) errors.push(key + "는 양수여야 합니다.");
    });
    ["mmrOpponentStrengthWeight", "visibleScoreOpponentStrengthWeight"].forEach(function (key) {
      if (!Number.isFinite(mmr[key]) || mmr[key] < 0 || mmr[key] > 2) {
        errors.push(key + "는 0 이상 2 이하여야 합니다.");
      }
    });
    ["scoreMarginMaxBonus", "clearMarginMaxBonus", "streakStep", "streakMaxBonus",
      "promotionScoreBonus", "promotionFailurePenalty", "demotionScorePenalty"].forEach(function (key) {
      if (!(mmr[key] >= 0)) errors.push(key + "는 0 이상이어야 합니다.");
    });
    if (!(mmr.outcomeDisagreementFactor > 0 && mmr.outcomeDisagreementFactor <= 1)) {
      errors.push("outcomeDisagreementFactor는 0 초과 1 이하여야 합니다.");
    }
    if (mmr.visibleScoreMaxFactor < mmr.visibleScoreMinFactor) {
      errors.push("visibleScoreMaxFactor는 visibleScoreMinFactor 이상이어야 합니다.");
    }
    if (["NEAREST", "FLOOR", "CEIL"].indexOf(mmr.deltaRounding) === -1) {
      errors.push("deltaRounding은 NEAREST/FLOOR/CEIL 중 하나여야 합니다.");
    }
  }

  if (!operational.placementPolicy) {
    errors.push("Placement 운영값을 모두 입력해야 합니다.");
  } else {
    var placement = operational.placementPolicy;
    var grades = placement.gradeThresholds;
    if (placement.stageCount !== 5) errors.push("placementStageCount는 5여야 합니다.");
    if (placement.mapsPerStage !== 2) errors.push("placementMapsPerStage는 2여야 합니다.");
    if (placement.roundSeconds !== 180) errors.push("placementRoundSeconds는 180이어야 합니다.");
    if (placement.finalAttemptWindowSeconds !== 10) {
      errors.push("placementFinalAttemptWindowSeconds는 10이어야 합니다.");
    }
    if (placement.prepareSeconds !== 10) errors.push("placementPrepareSeconds는 10이어야 합니다.");
    if (placement.levelRevealRemainingSeconds !== 5) {
      errors.push("placementLevelRevealRemainingSeconds는 5여야 합니다.");
    }
    if (!(500 >= grades.sMin && grades.sMin > grades.aMin && grades.aMin > grades.bMin &&
      grades.bMin > grades.cMin && grades.cMin > 0)) {
      errors.push("Placement Grade 기준은 500 >= S > A > B > C > 0이어야 합니다.");
    }
    if (placement.topAttemptCount !== 2) errors.push("placementTopAttemptCount는 2여야 합니다.");
    if (placement.clearTarget !== 2) errors.push("placementClearTarget은 2여야 합니다.");
    if (!(placement.transitionGuardSeconds >= 0 && placement.transitionGuardSeconds <= 1)) {
      errors.push("placementTransitionGuardSeconds는 0~1이어야 합니다.");
    }
    if (placement.poolRatingAnchors.length !== 6 || placement.poolRatingAnchors.some(function (value) {
      return !Number.isFinite(value);
    })) {
      errors.push("Placement Pool anchor는 6개 유한값이어야 합니다.");
    } else {
      for (var anchorIndex = 1; anchorIndex < placement.poolRatingAnchors.length; anchorIndex += 1) {
        if (placement.poolRatingAnchors[anchorIndex] <= placement.poolRatingAnchors[anchorIndex - 1]) {
          errors.push("Placement Pool anchor는 오름차순이어야 합니다.");
          break;
        }
      }
    }
    if (!(placement.mapScoreCenter >= 0 && placement.mapScoreCenter <= 500)) {
      errors.push("placementMapScoreCenter는 0~500이어야 합니다.");
    }
    if (!(placement.ratingPerMapScorePoint > 0)) {
      errors.push("placementRatingPerMapScorePoint는 양수여야 합니다.");
    }
    var placementWeight = placement.mapRatingWeight + placement.stageRatingWeight +
      placement.overallRatingWeight;
    if (placement.mapRatingWeight < 0 || placement.stageRatingWeight < 0 ||
      placement.overallRatingWeight < 0 || Math.abs(placementWeight - 1) > 0.000001) {
      errors.push("Placement Rating 비중 합은 1이어야 합니다.");
    }
  }

  if (!operational.timeouts) {
    errors.push("미확정 timeout 운영값을 모두 입력해야 합니다.");
  } else {
    Object.keys(operational.timeouts).forEach(function (key) {
      if (!(operational.timeouts[key] > 0)) errors.push(key + "는 양수여야 합니다.");
    });
  }

  if (!operational.matchmaking) {
    errors.push("Matchmaking MMR 범위 운영값을 모두 입력해야 합니다.");
  } else {
    var matchmaking = operational.matchmaking;
    if (matchmaking.initialRatingRange < 0 || matchmaking.widenPerSecond < 0) {
      errors.push("Matchmaking 범위와 초당 확장값은 음수일 수 없습니다.");
    }
    if (matchmaking.maximumRatingRange < matchmaking.initialRatingRange) {
      errors.push("최대 Matchmaking 범위는 초기 범위 이상이어야 합니다.");
    }
  }

  if (!operational.failurePolicy) {
    errors.push("장애/timeout 처리 정책을 모두 입력해야 합니다.");
  } else {
    if (["CANCEL_MATCH", "FORFEIT_UNREADY"].indexOf(operational.failurePolicy.readyTimeoutAction) === -1) {
      errors.push("readyTimeoutAction 값이 올바르지 않습니다.");
    }
    if (["CANCEL_MATCH", "FORFEIT_DISCONNECTED"].indexOf(operational.failurePolicy.reconnectTimeoutAction) === -1) {
      errors.push("reconnectTimeoutAction 값이 올바르지 않습니다.");
    }
    if (["CANCEL_MATCH", "RESUME"].indexOf(operational.failurePolicy.restartRecoveryAction) === -1) {
      errors.push("restartRecoveryAction 값이 올바르지 않습니다.");
    }
  }

  validateRankedClientPresentation_(client, errors);
  validateRankedPool_(maps, operational.tierBands || [], errors);
  validateRankedAllowedMods_(operational.cbf, allowedMods, errors);
  return { errors: errors };
}

function validateRankedCsmpSeeds_(seedsSheet, csmpTiersSheet, errors) {
  var seedNames = {};
  if (!seedsSheet || seedsSheet.getLastRow() < 2) {
    errors.push("Ranked CSMP Seed 시트에 NONE과 CSMP 티어 Seed를 입력해야 합니다.");
  } else {
    var seedValues = seedsSheet.getDataRange().getDisplayValues();
    var seedTierColumn = findOptionalHeaderIndex_(seedValues[0], ["CSMP Tier"]);
    var seedMmrColumn = findOptionalHeaderIndex_(seedValues[0], ["Initial Hidden MMR"]);
    if (seedTierColumn === -1 || seedMmrColumn === -1) {
      errors.push("Ranked CSMP Seed 헤더가 올바르지 않습니다.");
    } else {
      for (var seedRow = 1; seedRow < seedValues.length; seedRow += 1) {
        var rawSeedTier = rankedText_(seedValues[seedRow][seedTierColumn]);
        if (!rawSeedTier) continue;
        var seedTier = rawSeedTier.toUpperCase();
        if (seedTier.length > 80) {
          errors.push("Ranked CSMP Seed 티어명은 80자 이하여야 합니다: " + rawSeedTier);
          continue;
        }
        if (seedNames[seedTier]) {
          errors.push("Ranked CSMP Seed 티어명이 중복되어 있습니다: " + seedTier);
        }
        seedNames[seedTier] = true;
        var rawSeed = rankedText_(seedValues[seedRow][seedMmrColumn]);
        var seed = rawSeed === "" ? null : Number(rawSeed);
        if (!Number.isFinite(seed)) {
          errors.push("Ranked CSMP Seed의 " + seedTier + " 값을 입력해야 합니다.");
        }
      }
    }
  }

  if (!seedNames.NONE) errors.push("Ranked CSMP Seed에 NONE 행이 필요합니다.");

  if (!csmpTiersSheet || csmpTiersSheet.getLastRow() < 2) {
    errors.push("CSMP Tiers 시트에 하나 이상의 티어가 필요합니다.");
    return;
  }
  var tierValues = csmpTiersSheet.getDataRange().getDisplayValues();
  var tierNameColumn = findOptionalHeaderIndex_(
    tierValues[0],
    ["티어명", "티어 이름", "Tier", "Tier Name"],
  );
  if (tierNameColumn === -1) {
    errors.push("CSMP Tiers 티어명 헤더가 올바르지 않습니다.");
    return;
  }
  var csmpNames = {};
  for (var tierRow = 1; tierRow < tierValues.length; tierRow += 1) {
    var rawCsmpTier = rankedText_(tierValues[tierRow][tierNameColumn]);
    if (!rawCsmpTier) continue;
    var csmpTier = rawCsmpTier.toUpperCase();
    if (csmpTier === "NONE") {
      errors.push("CSMP Tiers에서 NONE은 Seed 기준값으로 예약되어 있습니다.");
      continue;
    }
    if (csmpTier.length > 80) {
      errors.push("CSMP Tiers 티어명은 80자 이하여야 합니다: " + rawCsmpTier);
      continue;
    }
    if (csmpNames[csmpTier]) {
      errors.push("CSMP Tiers 티어명이 중복되어 있습니다: " + csmpTier);
      continue;
    }
    csmpNames[csmpTier] = true;
    if (!seedNames[csmpTier]) {
      errors.push("Ranked CSMP Seed에 " + csmpTier + " 행과 MMR 값이 필요합니다.");
    }
  }
}

function validateRankedClientPresentation_(client, errors) {
  if (!client || !client.audio || !client.ui) return;
  var audio = client.audio;
  var ui = client.ui;

  if (!Number.isFinite(audio.fadeInSeconds) || audio.fadeInSeconds < 0 || audio.fadeInSeconds > 10) {
    errors.push("rankedAudioFadeInSeconds는 0~10초여야 합니다.");
  }
  if (!Number.isFinite(audio.fadeOutSeconds) || audio.fadeOutSeconds < 0 || audio.fadeOutSeconds > 10) {
    errors.push("rankedAudioFadeOutSeconds는 0~10초여야 합니다.");
  }
  if (!Number.isFinite(ui.fadeInSeconds) || ui.fadeInSeconds < 0 || ui.fadeInSeconds > 3) {
    errors.push("rankedUiFadeInSeconds는 0~3초여야 합니다.");
  }
  if (!Number.isFinite(ui.fadeOutSeconds) || ui.fadeOutSeconds < 0 || ui.fadeOutSeconds > 3) {
    errors.push("rankedUiFadeOutSeconds는 0~3초여야 합니다.");
  }

  var seen = {};
  (audio.resources || []).forEach(function (resource) {
    if (!resource.key) errors.push("Ranked resource key가 비어 있습니다.");
    if (!Number.isInteger(resource.songId) || resource.songId <= 0) {
      errors.push("Ranked resource Song ID는 양의 정수여야 합니다.");
    }
    if (!Number.isFinite(resource.startSeconds) || resource.startSeconds < 0) {
      errors.push("Ranked resource 시작 지점은 0초 이상이어야 합니다.");
    }
    if (seen[resource.key]) errors.push("Ranked resource key가 중복되어 있습니다: " + resource.key);
    seen[resource.key] = true;
  });

  if (audio.enabled && (audio.resources || []).length === 0) {
    errors.push("rankedAudioEnabled가 TRUE이면 하나 이상의 Song ID가 필요합니다.");
  }
}

function validateRankedTiers_(tierBands, errors) {
  var expected = ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"];
  var byTier = {};
  tierBands.forEach(function (band) {
    if (byTier[band.tier]) errors.push("Ranked Tiers에 " + band.tier + "가 중복되어 있습니다.");
    byTier[band.tier] = band;
  });
  expected.forEach(function (tier) {
    if (!byTier[tier]) errors.push("Ranked Tiers에 " + tier + "가 필요합니다.");
  });
  if (tierBands.length !== expected.length) {
    errors.push("Ranked Tiers에는 5개 표시 티어를 각각 한 번만 입력해야 합니다.");
  }

  var sorted = tierBands.slice().sort(function (left, right) {
    return Number(left.minInclusive) - Number(right.minInclusive);
  });
  sorted.forEach(function (band, index) {
    if (!Number.isFinite(band.minInclusive)) {
      errors.push(band.tier + " 최소 점수를 입력해야 합니다.");
    }
    if (band.maxExclusive !== null && !Number.isFinite(band.maxExclusive)) {
      errors.push(band.tier + " 최대 점수가 올바르지 않습니다.");
    }
    if (band.maxExclusive !== null && band.maxExclusive <= band.minInclusive) {
      errors.push(band.tier + " 최대 점수는 최소 점수보다 커야 합니다.");
    }
    if (index > 0) {
      var previous = sorted[index - 1];
      if (previous.maxExclusive !== band.minInclusive) {
        errors.push(previous.tier + "와 " + band.tier + " 경계는 빈 구간/중복 없이 이어져야 합니다.");
      }
    }
    if (!Number.isInteger(band.mainPool) || band.mainPool < 1 || band.mainPool > 6) {
      errors.push(band.tier + " Main Pool은 1~6 정수여야 합니다.");
    }
    if (!Number.isInteger(band.deathmatchPool) || band.deathmatchPool < 1 || band.deathmatchPool > 6) {
      errors.push(band.tier + " Deathmatch Pool은 1~6 정수여야 합니다.");
    }
    var highest = index === sorted.length - 1;
    if (!Number.isInteger(band.promotionGames) || band.promotionGames < 0 ||
      !Number.isInteger(band.promotionWinsRequired) || band.promotionWinsRequired < 0) {
      errors.push(band.tier + " 승급전 판수/필요 승수는 0 이상의 정수여야 합니다.");
    } else if (highest && (band.promotionGames !== 0 || band.promotionWinsRequired !== 0)) {
      errors.push(band.tier + "는 최상위 티어이므로 승급전 설정이 0/0이어야 합니다.");
    } else if (!highest && (band.promotionGames < 1 || band.promotionWinsRequired < 1 ||
      band.promotionWinsRequired > band.promotionGames)) {
      errors.push(band.tier + " 승급전 필요 승수는 전체 판수 이하여야 합니다.");
    }
  });
  if (sorted.length > 0 && sorted[sorted.length - 1].maxExclusive !== null) {
    errors.push("최상위 티어 최대 점수는 비워야 합니다.");
  }
}

function rankedCandidatePoolRequirements_(mainPool) {
  if (!Number.isInteger(mainPool) || mainPool < 1 || mainPool > 6) return null;
  if (mainPool === 1) return { 1: 2, 2: 2, 3: 1 };
  if (mainPool === 6) return { 4: 1, 5: 2, 6: 2 };
  var requirements = {};
  requirements[mainPool - 1] = 1;
  requirements[mainPool] = 3;
  requirements[mainPool + 1] = 1;
  return requirements;
}

function validateRankedPool_(maps, tierBands, errors) {
  var canonical = {};
  var counts = { 1: 0, 2: 0, 3: 0, 4: 0, 5: 0, 6: 0 };
  maps.forEach(function (map) {
    if (!map.active) return;
    if (!map.alternateLevelId || !/^[1-9]\d*$/.test(String(map.alternateLevelId))) {
      errors.push(
        "Ranked Pool 맵은 대체 맵 코드가 반드시 필요합니다. Canonical Level ID: " +
          (map.canonicalLevelId || "(없음)"),
      );
      return;
    }
    if (
      String(map.levelId) !== String(map.alternateLevelId) ||
      String(map.playableLevelId) !== String(map.alternateLevelId)
    ) {
      errors.push(
        "Ranked 실제 플레이 맵 코드는 대체 맵 코드와 정확히 같아야 합니다. Canonical Level ID: " +
          map.canonicalLevelId,
      );
      return;
    }
    if (!map.levelId || !map.canonicalLevelId || !map.title || !map.creator || !map.difficulty) {
      errors.push("Ranked Pool 활성 맵의 필수 텍스트 값이 비어 있습니다: " + (map.levelId || "(Level ID 없음)"));
      return;
    }
    if (!Number.isInteger(map.pool) || map.pool < 1 || map.pool > 6) {
      errors.push("Ranked Pool 값은 1~6 정수여야 합니다: " + map.levelId);
      return;
    }
    if (!Number.isFinite(map.qualifyingPercent) || map.qualifyingPercent < 0 || map.qualifyingPercent > 100) {
      errors.push("Qualifying %는 0~100이어야 합니다: " + map.levelId);
      return;
    }
    var existing = canonical[map.canonicalLevelId];
    if (existing) {
      if (
        existing.pool !== map.pool ||
        existing.qualifyingPercent !== map.qualifyingPercent ||
        existing.title !== map.title ||
        existing.creator !== map.creator ||
        existing.difficulty !== map.difficulty
      ) {
        errors.push("같은 Canonical Level의 활성 등록 정보가 충돌합니다: " + map.canonicalLevelId);
      }
      return;
    }
    canonical[map.canonicalLevelId] = map;
    counts[map.pool] += 1;
  });

  for (var placementPool = 1; placementPool <= 6; placementPool += 1) {
    if (counts[placementPool] < 2) {
      errors.push(
        "Placement Pool " + placementPool + "에는 서로 다른 활성 canonical 맵이 최소 2개 필요합니다.",
      );
    }
  }

  tierBands.forEach(function (band) {
    var requirements = rankedCandidatePoolRequirements_(band.mainPool);
    if (!requirements) return;
    Object.keys(requirements).forEach(function (pool) {
      var required = requirements[pool];
      if (counts[pool] < required) {
        errors.push(
          band.tier + " Main Pool " + band.mainPool +
          " 후보 생성을 위한 Pool " + pool + " canonical 맵이 부족합니다.",
        );
      }
    });
  });
}

function validateRankedAllowedMods_(cbf, allowedMods, errors) {
  var enabledById = {};
  allowedMods.forEach(function (rule) {
    if (!rule.enabled) return;
    if (enabledById[rule.id]) errors.push("Ranked Allowed Mods에 Mod ID가 중복되어 있습니다: " + rule.id);
    enabledById[rule.id] = rule;
  });
  ["hwanhee1.corum_ranked", "geode.node-ids", cbf && cbf.modId].forEach(function (id) {
    if (!id || !enabledById[id] || !enabledById[id].required) {
      errors.push("필수 모드는 Enabled/Required 상태여야 합니다: " + (id || "CBF Mod ID 없음"));
    }
  });
}

function rankedStableHash_(text) {
  var hash = 2166136261;
  for (var index = 0; index < text.length; index += 1) {
    hash ^= text.charCodeAt(index);
    hash = Math.imul(hash, 16777619);
  }
  return (hash >>> 0).toString(16).padStart(8, "0");
}
