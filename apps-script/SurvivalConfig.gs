/** Independent Survival operations, in the existing Apps Script deployment. */
var CORUM_SURVIVAL_CONFIG_SHEET = "Survival Config";
var CORUM_SURVIVAL_CONFIG_DEFAULTS = Object.freeze([
  ["enabled", false, "Survival만 제어. Solo Ranked의 enabled와 독립"],
  ["initialLp", 150, "최초 조회 시 LP. 기존 프로필에는 재적용하지 않음"],
  ["placementEnabled", false, "M1: 배치 경기 없음"],
  ["promotionSeriesEnabled", false, "M1: 승급전 없음"],
  ["demotionProtectionEnabled", true, "티어 하한에서 1회 보호"],
  ["matchPlayerCount", 16, "정규 경기 인원. 실제 매칭은 M5"],
  ["minimumPlayers", 8, "최소 시작 인원. 부족 인원 진출/LP 규칙은 M3/M5"],
  ["matchmakingMaxWaitSeconds", 180, "대기 한도 설정. 실행은 M5"],
  ["tierMatchingEnabled", false, "티어별 매칭 여부"],
  ["roundSeconds", 180, "라운드 시간"],
  ["round1Qualifiers", 10, "16인 경기 1라운드 진출 인원"],
  ["round2Qualifiers", 6, "16인 경기 2라운드 진출 인원"],
  ["round3Qualifiers", 3, "16인 경기 3라운드 진출 인원"],
  ["finalWinners", 1, "결승 우승 인원"],
  ["allowCutoffTies", true, "커트라인 공동 순위 허용"],
  ["lastAttemptEnabled", false, "M1: LAST ATTEMPT 없음"],
  ["scoreMultiplier", 1, "점수 배율"],
  ["tiebreakAttempts", 6, "추가 결승 attempt 수"],
  ["tiebreakDecimals", 3, "추가 결승 소수 자릿수"],
  ["minimumLevelId", 40000000, "맵 ID 하한"],
  ["demotionWarningRecoveryLp", 50, "현재 티어 하한 + 이 LP 이상 회복 시 경고 해제"],
  ["demotionTargetOffsetLp", 50, "강등 전 티어 하한 - 이 LP로 강등"],
  ["tierBands", '[{"tier":"RED","group":"RED","minimumLp":0},{"tier":"AQUA","group":"AQUA","minimumLp":300},{"tier":"BRONZE","group":"BRONZE","minimumLp":600},{"tier":"SILVER","group":"SILVER","minimumLp":900},{"tier":"GOLD","group":"GOLD","minimumLp":1200}]', "Survival 전용 JSON. tier=고유 ID, group=아이콘 그룹, minimumLp=하한. 세분화 가능"],
  ["placementLp", '[50,40,30,25,20,15,10,8,5,2,-10,-20,-30,-40,-50,-55]', "순위별 LP JSON. 공동 순위는 차지한 순위 중 최저 LP"],
]);

/** Add only missing defaults; never overwrite operator values or Solo sheets. */
function setupCorumSurvivalConfig() {
  var sheet = getOrCreateSheet_(getSpreadsheet_(), CORUM_SURVIVAL_CONFIG_SHEET, CORUM_RANKED_CONFIG_HEADERS);
  var values = sheet.getDataRange().getValues();
  var headers = values[0];
  var keyColumn = findOptionalHeaderIndex_(headers, ["Key"]);
  var valueColumn = findOptionalHeaderIndex_(headers, ["Value"]);
  var descriptionColumn = findOptionalHeaderIndex_(headers, ["설명"]);
  var existing = {};
  values.slice(1).forEach(function (row) {
    var key = rankedText_(row[keyColumn]).toLowerCase();
    if (key) existing[key] = true;
  });
  CORUM_SURVIVAL_CONFIG_DEFAULTS.forEach(function (defaults) {
    if (existing[defaults[0].toLowerCase()]) return;
    var row = Array(headers.length).fill("");
    row[keyColumn] = defaults[0];
    row[valueColumn] = defaults[1];
    row[descriptionColumn] = defaults[2];
    sheet.appendRow(row);
  });
  sheet.setFrozenRows(1);
  var document = readCorumSurvivalConfig_();
  console.log(document.validation.valid ? "Corum Survival 설정 준비 완료" : document.validation.errors.join("; "));
  return document.validation;
}

function getCorumSurvivalConfigResponse_() {
  return json_({ ok: true, action: "survival_config", data: readCorumSurvivalConfig_() });
}

function readCorumSurvivalConfig_() {
  var sheet = getSpreadsheet_().getSheetByName(CORUM_SURVIVAL_CONFIG_SHEET);
  var raw = readRankedKeyValues_(sheet);
  var errors = [];
  if (!sheet) errors.push("Survival Config is missing; run setupCorumSurvivalConfig");
  var config = {};
  var booleanKeys = ["enabled", "placementEnabled", "promotionSeriesEnabled", "demotionProtectionEnabled", "tierMatchingEnabled", "allowCutoffTies", "lastAttemptEnabled"];
  var integerKeys = ["initialLp", "matchPlayerCount", "minimumPlayers", "matchmakingMaxWaitSeconds", "roundSeconds", "round1Qualifiers", "round2Qualifiers", "round3Qualifiers", "finalWinners", "tiebreakAttempts", "tiebreakDecimals", "minimumLevelId", "demotionWarningRecoveryLp", "demotionTargetOffsetLp"];
  booleanKeys.forEach(function (key) {
    var value = rankedText_(raw[key]).toLowerCase();
    if (["true", "false", "1", "0", "yes", "no", "y", "n", "활성", "비활성"].indexOf(value) === -1) errors.push(key + " must be TRUE/FALSE");
    config[key] = rankedBoolean_(raw[key]);
  });
  integerKeys.forEach(function (key) { config[key] = rankedNumber_(raw[key]); });
  config.scoreMultiplier = rankedNumber_(raw.scoreMultiplier);
  ["tierBands", "placementLp"].forEach(function (key) {
    try { config[key] = JSON.parse(rankedText_(raw[key])); }
    catch (error) { config[key] = []; errors.push(key + " must be valid JSON"); }
  });
  // Survival remains unsplit. Its five boundaries follow Solo's III divisions.
  var soloSheet = getSpreadsheet_().getSheetByName(CORUM_RANKED_SHEETS.tiers);
  var soloBands = readRankedTierBands_(soloSheet);
  ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"].forEach(function (base) {
    var boundary = soloBands.filter(function (band) { return band.tier === (base === "GOLD" ? base : base + "3") || band.tier === base; })[0];
    if (boundary && Number.isSafeInteger(boundary.minInclusive)) {
      config.tierBands.forEach(function (band) { if (band.group === base) band.minimumLp = boundary.minInclusive; });
    }
  });
  // A duplicate key must be fixed by the operator rather than silently winning.
  if (sheet && sheet.getLastRow() > 1) {
    var values = sheet.getDataRange().getValues();
    var column = findOptionalHeaderIndex_(values[0], ["Key"]), seen = {};
    values.slice(1).forEach(function (row) {
      var key = rankedText_(row[column]).toLowerCase();
      if (key && seen[key]) errors.push("Duplicate Survival key: " + key);
      seen[key] = true;
    });
  }
  errors = errors.concat(validateCorumSurvivalConfig_(config));
  return { generation: "survival-" + rankedStableHash_(JSON.stringify(config)),
    generatedAt: new Date().toISOString(), config: config,
    validation: { valid: errors.length === 0, errors: errors } };
}

function validateCorumSurvivalConfig_(config) {
  var errors = [];
  ["initialLp", "matchPlayerCount", "minimumPlayers", "matchmakingMaxWaitSeconds", "roundSeconds", "round1Qualifiers", "round2Qualifiers", "round3Qualifiers", "finalWinners", "tiebreakAttempts", "tiebreakDecimals", "minimumLevelId", "demotionWarningRecoveryLp", "demotionTargetOffsetLp"].forEach(function (key) {
    if (!Number.isSafeInteger(config[key]) || config[key] < 0) errors.push(key + " must be a non-negative integer");
  });
  if (config.placementEnabled || config.promotionSeriesEnabled || config.lastAttemptEnabled) errors.push("M1 placement, promotion series and LAST ATTEMPT must remain FALSE");
  if (!(config.minimumPlayers >= 8 && config.minimumPlayers <= config.matchPlayerCount && config.matchPlayerCount <= 16)) errors.push("Player counts must satisfy 8 <= minimumPlayers <= matchPlayerCount <= 16");
  ["matchmakingMaxWaitSeconds", "roundSeconds", "tiebreakAttempts", "minimumLevelId"].forEach(function (key) { if (!(config[key] > 0)) errors.push(key + " must be positive"); });
  if (config.tiebreakDecimals > 6) errors.push("tiebreakDecimals must be <= 6");
  if (!(config.scoreMultiplier > 0) || !Number.isFinite(config.scoreMultiplier)) errors.push("scoreMultiplier must be positive");
  if (!(config.matchPlayerCount > config.round1Qualifiers && config.round1Qualifiers > config.round2Qualifiers && config.round2Qualifiers > config.round3Qualifiers && config.round3Qualifiers > config.finalWinners && config.finalWinners > 0)) errors.push("Nominal qualifiers must strictly decrease");
  if (!(config.demotionTargetOffsetLp > 0)) errors.push("demotionTargetOffsetLp must be positive");
  var bands = config.tierBands, ids = {};
  if (!Array.isArray(bands) || !bands.length) errors.push("tierBands must be a nonempty array");
  else bands.forEach(function (band, index) {
    if (!band || !/^[A-Z][A-Z0-9_]{0,39}$/.test(band.tier) || ids[band.tier] || ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"].indexOf(band.group) === -1 || !Number.isSafeInteger(band.minimumLp) || band.minimumLp < 0) { errors.push("Invalid or duplicate tierBands entry " + index); return; }
    ids[band.tier] = true;
    if (index === 0 && band.minimumLp !== 0) errors.push("Lowest tier must start at 0 LP");
    if (index > 0 && (!bands[index - 1] || !(band.minimumLp > bands[index - 1].minimumLp) || config.demotionTargetOffsetLp > band.minimumLp - bands[index - 1].minimumLp)) errors.push("Tier boundaries must increase and contain the demotion offset");
  });
  if (!Array.isArray(config.placementLp) || config.placementLp.length !== config.matchPlayerCount || config.placementLp.some(function (lp) { return !Number.isSafeInteger(lp); })) errors.push("placementLp must provide one integer for every nominal rank");
  return errors;
}
