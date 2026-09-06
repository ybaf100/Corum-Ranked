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
  Object.freeze(["placementPoolMoveS", 2, "S Grade 다음 Stage Pool 이동량"]),
  Object.freeze(["placementPoolMoveA", 1, "A Grade 다음 Stage Pool 이동량"]),
  Object.freeze(["placementPoolMoveB", 0, "B Grade 다음 Stage Pool 이동량"]),
  Object.freeze(["placementPoolMoveC", -1, "C Grade 다음 Stage Pool 이동량"]),
  Object.freeze(["placementPoolMoveD", -2, "D Grade 다음 Stage Pool 이동량"]),
  Object.freeze(["placementSkipVisibleScore", 150, "Placement Skip 시 RED 고정 LP"]),
  Object.freeze(["placementSkipHiddenMmr", 150, "Placement Skip 시 hidden MMR"]),
  Object.freeze(["placementPoolCutoffMargin", 15, "Placement 종료 시 Pool cutoff ±margin"]),
]);

function setupCorumRankedSheets() {
  var ss = SpreadsheetApp.getActiveSpreadsheet();
  var tiersSheet = getOrCreateRankedSheet_(ss, CORUM_RANKED_SHEETS.tiers, CORUM_RANKED_TIER_HEADERS);
  var seedsSheet = getOrCreateRankedSheet_(ss, CORUM_RANKED_SHEETS.seeds, CORUM_RANKED_SEED_HEADERS);
  var allowedModsSheet = getOrCreateRankedSheet_(ss, CORUM_RANKED_SHEETS.allowedMods, CORUM_RANKED_ALLOWED_MOD_HEADERS);
  var configSheet = getOrCreateRankedSheet_(ss, CORUM_RANKED_SHEETS.config, CORUM_RANKED_CONFIG_HEADERS);
  var csmpTiersSheet = ss.getSheetByName("tiers");
  appendMissingRankedRows_(tiersSheet, CORUM_RANKED_TIER_DEFAULTS);
  appendMissingRankedCsmpSeedRows_(seedsSheet, csmpTiersSheet);
  appendMissingRankedRows_(allowedModsSheet, CORUM_RANKED_ALLOWED_MOD_DEFAULTS);
  appendMissingRankedRows_(configSheet, CORUM_RANKED_CONFIG_DEFAULTS);
}

function getOrCreateRankedSheet_(ss, name, headers) {
  var sheet = ss.getSheetByName(name);
  if (!sheet) sheet = ss.insertSheet(name);
  if (sheet.getLastRow() === 0) sheet.appendRow(headers);
  return sheet;
}

function appendMissingRankedRows_(sheet, defaults) {
  var lastRow = sheet.getLastRow();
  var existing = lastRow > 1
    ? sheet.getRange(2, 1, lastRow - 1, 1).getValues().map(function(row) { return String(row[0]).trim(); })
    : [];
  defaults.forEach(function(row) {
    if (existing.indexOf(String(row[0])) === -1) sheet.appendRow(row);
  });
}

function appendMissingRankedCsmpSeedRows_(sheet, tiersSheet) {
  var defaults = CORUM_RANKED_SEED_DEFAULTS.slice();
  if (tiersSheet && tiersSheet.getLastRow() > 1) {
    var names = tiersSheet.getRange(2, 1, tiersSheet.getLastRow() - 1, 1).getValues();
    names.forEach(function(row) {
      var name = String(row[0]).trim();
      if (name && !defaults.some(function(item) { return item[0] === name; })) {
        defaults.push(Object.freeze([name, ""]));
      }
    });
  }
  appendMissingRankedRows_(sheet, defaults);
}
