import assert from "node:assert/strict";
import fs from "node:fs";
import vm from "node:vm";

class MockRange {
  constructor(sheet, row, column, rowCount, columnCount) {
    this.sheet = sheet;
    this.row = row;
    this.column = column;
    this.rowCount = rowCount;
    this.columnCount = columnCount;
  }

  getValues() {
    return Array.from({ length: this.rowCount }, (_, rowOffset) =>
      Array.from({ length: this.columnCount }, (_, columnOffset) =>
        this.sheet.rows[this.row - 1 + rowOffset]?.[this.column - 1 + columnOffset] ?? "",
      ),
    );
  }

  getDisplayValues() {
    return this.getValues().map((row) => row.map((value) => String(value ?? "")));
  }

  setValues(values) {
    values.forEach((row, rowOffset) => {
      const rowIndex = this.row - 1 + rowOffset;
      if (!this.sheet.rows[rowIndex]) this.sheet.rows[rowIndex] = [];
      row.forEach((value, columnOffset) => {
        this.sheet.rows[rowIndex][this.column - 1 + columnOffset] = value;
      });
    });
    return this;
  }

  setValue(value) {
    return this.setValues([[value]]);
  }
}

class MockSheet {
  constructor(name, rows = []) {
    this.name = name;
    this.rows = rows.map((row) => [...row]);
  }

  getName() {
    return this.name;
  }

  getLastRow() {
    return this.rows.length;
  }

  getLastColumn() {
    return Math.max(0, ...this.rows.map((row) => row.length));
  }

  getDataRange() {
    return new MockRange(
      this,
      1,
      1,
      Math.max(1, this.getLastRow()),
      Math.max(1, this.getLastColumn()),
    );
  }

  getRange(row, column, rowCount = 1, columnCount = 1) {
    return new MockRange(this, row, column, rowCount, columnCount);
  }

  appendRow(row) {
    this.rows.push([...row]);
  }

  setFrozenRows() {}
}

class MockSpreadsheet {
  constructor() {
    const headers = [
      "순위",
      "맵 제목",
      "Rating",
      "맵 길이",
      "맵 코드",
      "대체 맵 코드",
      "제작자",
      "Verifier",
      "최소 등록 가능 기록",
      "CSMP 티어 배정",
    ];
    const maps = [];
    for (let pool = 1; pool <= 6; pool += 1) {
      for (let index = 1; index <= 5; index += 1) {
        maps.push([
          (pool - 1) * 5 + index,
          `Map ${pool}-${index}`,
          "Extreme Demon",
          "Long",
          `${pool}${index}001`,
          pool === 5 && index === 1 ? "987654321" : `8${pool}${index}001`,
          "Creator",
          "Verifier",
          50,
          "Gold",
        ]);
      }
    }
    const integrationMaps = maps.map((row) => {
      const copy = [...row];
      copy[1] = `Integration ${copy[1]}`;
      copy[4] = `9${copy[4]}`;
      copy[5] = `7${copy[5]}`;
      return copy;
    });
    this.sheets = [
      new MockSheet("sheet1", [headers, ...integrationMaps]),
      new MockSheet("sheet2", [headers, ...maps]),
    ];
  }

  getSheetByName(name) {
    return this.sheets.find((sheet) => sheet.getName() === name) || null;
  }

  getSheets() {
    return [...this.sheets];
  }

  insertSheet(name) {
    const sheet = new MockSheet(name);
    this.sheets.push(sheet);
    return sheet;
  }
}

const spreadsheet = new MockSpreadsheet();
const context = {
  console,
  ContentService: {
    MimeType: { JSON: "application/json" },
    createTextOutput(text) {
      return {
        text,
        setMimeType() {
          return this;
        },
      };
    },
  },
  PropertiesService: {
    getScriptProperties() {
      return {
        getProperty() {
          return "";
        },
      };
    },
  },
  SpreadsheetApp: {
    getActiveSpreadsheet() {
      return spreadsheet;
    },
  },
};

vm.createContext(context);
vm.runInContext(fs.readFileSync(new URL("./Code.gs", import.meta.url), "utf8"), context);
vm.runInContext(fs.readFileSync(new URL("./RankedConfig.gs", import.meta.url), "utf8"), context);
assert.equal(context.rankedPercent_(0.35, "35%"), 35);
assert.equal(context.rankedPercent_(1, "1"), 1);

const responseJson = () =>
  JSON.parse(context.doGet({ parameter: { action: "ranked_config" } }).text);

const setConfig = (key, value) => {
  const sheet = spreadsheet.getSheetByName("Ranked Config");
  const row = sheet.rows.find((candidate) => candidate[0] === key);
  assert.ok(row, `Missing config row ${key}`);
  row[1] = value;
};

context.setupCorumRankedConfig();
for (const name of [
  "Ranked Tiers",
  "Ranked CSMP Seed",
  "Ranked Allowed Mods",
  "Ranked Config",
]) {
  assert.ok(spreadsheet.getSheetByName(name), `Missing ${name}`);
}
assert.equal(spreadsheet.getSheetByName("Ranked Pool"), null);
const allowedModRows = spreadsheet.getSheetByName("Ranked Allowed Mods").rows;
const nodeIdsRow = allowedModRows.find((row) => row[0] === "geode.node-ids");
assert.ok(nodeIdsRow, "Node IDs must be created in Ranked Allowed Mods");
assert.equal(nodeIdsRow[2], "v1.23.3");
assert.equal(nodeIdsRow[4], true);
assert.equal(nodeIdsRow[5], true);

const integrationMapSheet = spreadsheet.getSheetByName("sheet1");
const mapSheet = spreadsheet.getSheetByName("sheet2");
assert.equal(integrationMapSheet.rows[0].includes("Ranked Pool (1~6)"), false);
assert.equal(integrationMapSheet.rows[0].includes("Qualifying %"), false);
const poolColumn = mapSheet.rows[0].indexOf("Ranked Pool (1~6)");
const qualifyingColumn = mapSheet.rows[0].indexOf("Qualifying %");
assert.ok(poolColumn >= 0);
assert.ok(qualifyingColumn >= 0);
for (let index = 1; index < mapSheet.rows.length; index += 1) {
  mapSheet.rows[index][poolColumn] = Math.floor((index - 1) / 5) + 1;
  mapSheet.rows[index][qualifyingColumn] = index === 1 ? "35%" : 50;
}
setConfig("ruleVersion", "corum-ranked-v0.3");
setConfig("roundSeconds", 180);
setConfig("clearTarget", 2);
context.setupCorumRankedConfig();
assert.equal(mapSheet.rows[0].filter((value) => value === "Ranked Pool (1~6)").length, 1);
assert.equal(mapSheet.rows[0].filter((value) => value === "Qualifying %").length, 1);
assert.equal(spreadsheet.getSheetByName("Ranked Config").rows.find((row) => row[0] === "ruleVersion")[1], "corum-ranked-v0.5");
assert.equal(spreadsheet.getSheetByName("Ranked Config").rows.find((row) => row[0] === "roundSeconds")[1], 300);
assert.equal(spreadsheet.getSheetByName("Ranked Config").rows.find((row) => row[0] === "clearTarget")[1], 3);
assert.equal(spreadsheet.getSheetByName("Ranked Config").rows.find((row) => row[0] === "mmrOpponentStrengthWeight")[1], 1);
assert.equal(spreadsheet.getSheetByName("Ranked Config").rows.find((row) => row[0] === "visibleScoreOpponentStrengthWeight")[1], 1);

const initial = responseJson();
assert.equal(initial.ok, true);
assert.equal(initial.action, "ranked_config");
assert.equal(initial.data.operational.enabled, false);
assert.equal(initial.data.validation.valid, false);
assert.equal(initial.data.validation.queueReady, false);
assert.equal(initial.data.operational.mmrPolicy, undefined);
assert.equal(initial.data.operational.timeouts, undefined);

const tierRows = spreadsheet.getSheetByName("Ranked Tiers").rows;
const tierRanges = {
  RED: [0, 1000],
  AQUA: [1000, 2000],
  BRONZE: [2000, 3000],
  SILVER: [3000, 4000],
  GOLD: [4000, ""],
};
for (const row of tierRows.slice(1)) {
  const base = row[0].replace(/[123]$/, "");
  const [min,max] = tierRanges[base];
  const index = base === "GOLD" ? 0 : 3-Number(row[0].slice(-1));
  row[1] = base === "GOLD" ? min : Math.ceil(min+(max-min)*index/3);
  row[2] = base === "GOLD" ? "" : Math.ceil(min+(max-min)*(index+1)/3);
}

const seedRows = spreadsheet.getSheetByName("Ranked CSMP Seed").rows;
const seeds = { NONE: 500, RED: 700, AQUA: 1500, BRONZE: 2500, SILVER: 3500, GOLD: 4500 };
for (const row of seedRows.slice(1)) row[1] = seeds[row[0]];

for (const [key, value] of Object.entries({
  enabled: true,
  placementGames: 5,
  placementKFactor: 64,
  regularKFactor: 32,
  expectedScoreDivisor: 400,
  deltaRounding: "NEAREST",
  sessionSeconds: 3600,
  readySeconds: 30,
  reconnectGraceSeconds: 20,
  queueHeartbeatSeconds: 15,
  matchHeartbeatSeconds: 10,
  orphanAttemptSeconds: 120,
  roundResultSeconds: 5,
  matchmakingInitialRatingRange: 100,
  matchmakingWidenPerSecond: 2,
  matchmakingMaximumRatingRange: 500,
  readyTimeoutAction: "CANCEL_MATCH",
  reconnectTimeoutAction: "FORFEIT_DISCONNECTED",
  restartRecoveryAction: "RESUME",
})) {
  setConfig(key, value);
}

const complete = responseJson();
assert.equal(complete.data.validation.valid, true, complete.data.validation.errors.join("\n"));
assert.equal(complete.data.validation.queueReady, true);
assert.equal(complete.data.maps.length, 30);
assert.equal(complete.data.maps[0].canonicalLevelId, "11001");
assert.equal(complete.data.maps[0].alternateLevelId, "811001");
assert.equal(complete.data.maps[0].playableLevelId, "811001");
assert.equal(complete.data.maps[0].levelId, "811001");
assert.equal(complete.data.maps[0].qualifyingPercent, 35);
assert.equal(complete.data.maps[0].length, "Long");
const alternate = complete.data.maps.find((map) => map.canonicalLevelId === "51001");
assert.equal(alternate.alternateLevelId, "987654321");
assert.equal(alternate.playableLevelId, "987654321");
assert.equal(alternate.levelId, "987654321");
assert.equal(complete.data.operational.rules.rulesVersion, "corum-ranked-v0.5");
assert.equal(complete.data.operational.rules.roundSeconds, 300);
assert.equal(complete.data.operational.rules.clearTarget, 3);
assert.equal(complete.data.operational.mmrPolicy.mmrOpponentStrengthWeight, 1);
assert.equal(complete.data.operational.mmrPolicy.visibleScoreOpponentStrengthWeight, 1);
assert.deepEqual(
  complete.data.operational.csmpTierNames,
  ["RED", "AQUA", "BRONZE", "SILVER", "GOLD"],
);
assert.equal(complete.data.operational.cbf.requiredSettings["soft-toggle"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["click-on-steps"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["physics-bypass"], false);
assert.equal(complete.data.operational.generation, complete.data.generation);
assert.equal(complete.data.client.audio.enabled, false);
assert.deepEqual(complete.data.client.audio.resources, []);
assert.equal(complete.data.client.audio.fadeInSeconds, 0);
assert.equal(complete.data.client.audio.fadeOutSeconds, 0);
assert.equal(complete.data.client.ui.fadeInSeconds, 0.24);
assert.equal(complete.data.client.ui.fadeOutSeconds, 0.18);

setConfig("mmrOpponentStrengthWeight", 2.01);
const invalidMmrOpponentWeight = responseJson();
assert.equal(invalidMmrOpponentWeight.data.validation.valid, false);
assert.ok(invalidMmrOpponentWeight.data.validation.errors.some((error) =>
  error.includes("mmrOpponentStrengthWeight"),
));
setConfig("mmrOpponentStrengthWeight", 1);
setConfig("visibleScoreOpponentStrengthWeight", -0.01);
const invalidVisibleOpponentWeight = responseJson();
assert.equal(invalidVisibleOpponentWeight.data.validation.valid, false);
assert.ok(invalidVisibleOpponentWeight.data.validation.errors.some((error) =>
  error.includes("visibleScoreOpponentStrengthWeight"),
));
setConfig("visibleScoreOpponentStrengthWeight", 1);

// Stage Placement requires two canonical maps in every Pool even when the PvP
// candidate distribution would consume only one from that Pool.
const removedPoolOneValues = [];
for (let index = 2; index <= 5; index += 1) {
  removedPoolOneValues.push(mapSheet.rows[index][poolColumn]);
  mapSheet.rows[index][poolColumn] = "";
}
const insufficientPlacementPool = responseJson();
assert.equal(insufficientPlacementPool.data.validation.valid, false);
assert.ok(insufficientPlacementPool.data.validation.errors.some((error) =>
  error.includes("Placement Pool 1"),
));
const redTierRow = tierRows.find((row) => row[0] === "RED3");
redTierRow[3] = 1;
const shiftedMainPool = responseJson();
assert.equal(shiftedMainPool.data.validation.valid, false);
assert.ok(shiftedMainPool.data.validation.errors.some((error) => error.includes("RED3 Main Pool 1")));
redTierRow[3] = 2;
for (let index = 2; index <= 5; index += 1) {
  mapSheet.rows[index][poolColumn] = removedPoolOneValues[index - 2];
}

// New CSMP tier rows automatically receive a Seed row. They fail closed until
// an MMR is entered, then appear in the dynamic config without changing the
// five fixed Ranked display tiers.
const csmpTierSheet = spreadsheet.getSheetByName("CSMP Tiers");
csmpTierSheet.appendRow([6, "Diamond", "Diamond.png", "#88AAFF", 1]);
context.setupCorumRankedConfig();
const diamondSeedRow = seedRows.find((row) => row[0] === "DIAMOND");
assert.ok(diamondSeedRow);
assert.equal(diamondSeedRow[1], "");
assert.equal(responseJson().data.validation.valid, false);
diamondSeedRow[1] = 5200;
const withDynamicCsmp = responseJson();
assert.equal(withDynamicCsmp.data.validation.valid, true, withDynamicCsmp.data.validation.errors.join("\n"));
assert.equal(withDynamicCsmp.data.operational.csmpSeeds.DIAMOND, 5200);
assert.ok(withDynamicCsmp.data.operational.csmpTierNames.includes("DIAMOND"));
csmpTierSheet.appendRow([7, " diamond ", "Diamond2.png", "#88AAFF", 1]);
const duplicateCsmp = responseJson();
assert.equal(duplicateCsmp.data.validation.valid, false);
assert.ok(duplicateCsmp.data.validation.errors.some((error) => error.includes("CSMP Tiers 티어명이 중복")));
csmpTierSheet.rows.pop();

setConfig("rankedAudioMenuSongId", 123456);
setConfig("rankedAudioMenuStartSeconds", 42.5);
setConfig("rankedAudioDemotionWarningLobbySongId", 123457);
setConfig("rankedAudioDemotionWarningLobbyStartSeconds", 5);
setConfig("rankedAudioPromotionLobbySongId", 123458);
setConfig("rankedAudioPromotionLobbyStartSeconds", 7.5);
setConfig("rankedAudioMatchSongId", 234567);
setConfig("rankedAudioMatchStartSeconds", 10);
setConfig("rankedAudioRound3DeathmatchWaitSongId", 234568);
setConfig("rankedAudioRound3DeathmatchWaitStartSeconds", 12);
setConfig("rankedAudioEnabled", true);
const withResources = responseJson();
assert.equal(withResources.data.validation.valid, true, withResources.data.validation.errors.join("\n"));
assert.equal(withResources.data.client.audio.enabled, true);
assert.deepEqual(
  withResources.data.client.audio.resources.map((resource) => [resource.key, resource.songId, resource.startSeconds]),
  [
    ["menu", 123456, 42.5],
    ["menu_demotion_warning", 123457, 5],
    ["menu_promotion", 123458, 7.5],
    ["match", 234567, 10],
    ["round3_deathmatch_wait", 234568, 12],
  ],
);

const repeated = responseJson();
assert.equal(repeated.data.generation, withResources.data.generation);

// Library IDs use the same spreadsheet keys and protocol fields as NG songs.
setConfig("rankedAudioMenuSongId", "10000001");
setConfig("rankedAudioWinSongId", 10000002);
setConfig("rankedAudioWinStartSeconds", 12.25);
const withLibrary = responseJson();
assert.equal(withLibrary.data.validation.valid, true, withLibrary.data.validation.errors.join("\n"));
assert.deepEqual(
  withLibrary.data.client.audio.resources.filter((resource) => ["menu", "result_win"].includes(resource.key))
    .map((resource) => [resource.key, resource.songId, resource.startSeconds, resource.loop]),
  [["menu", 10000001, 42.5, true], ["result_win", 10000002, 12.25, false]],
);
assert.notEqual(withLibrary.data.generation, repeated.data.generation);

// Images use an immutable server-selected tag, with one ZIP digest/size.
assert.equal(repeated.data.client.assets.enabled, false);
setConfig("rankedAssetPackEnabled", true);
assert.equal(responseJson().data.validation.valid, false);
setConfig("rankedAssetPackVersion", "visuals-v1");
setConfig("rankedAssetPackSha256", "A".repeat(64));
setConfig("rankedAssetPackSizeBytes", 24957530);
const withAssets = responseJson();
assert.equal(withAssets.data.validation.valid, true, withAssets.data.validation.errors.join("\n"));
assert.deepEqual(withAssets.data.client.assets, {
  enabled: true, assetPackVersion: "visuals-v1", sha256: "a".repeat(64), sizeBytes: 24957530,
});
assert.notEqual(withAssets.data.generation, repeated.data.generation);
for (const invalid of ["../other", ".", "..", "a/b", "x".repeat(65)]) {
  setConfig("rankedAssetPackVersion", invalid);
  assert.equal(responseJson().data.validation.valid, false);
}
setConfig("rankedAssetPackVersion", "visuals-v2");
setConfig("rankedAssetPackSha256", "wrong");
assert.equal(responseJson().data.validation.valid, false);
setConfig("rankedAssetPackSha256", "b".repeat(64));
for (const invalid of [0, -1, 2.5, 67108865]) {
  setConfig("rankedAssetPackSizeBytes", invalid);
  assert.equal(responseJson().data.validation.valid, false);
}
setConfig("rankedAssetPackSizeBytes", 25000000);
assert.equal(responseJson().data.validation.valid, true);
mapSheet.rows[1][qualifyingColumn] = 55;
const changed = responseJson();
assert.notEqual(changed.data.generation, withResources.data.generation);

mapSheet.rows[2][poolColumn] = "";
const excluded = responseJson();
assert.equal(excluded.data.maps.length, 29);

// Four input columns suffice; the "alternate" column IS the playable map ID.
// Setup must respect the operator's Tier Pool alias, not append a blank shadow.
const fullMapRows = mapSheet.rows;
mapSheet.rows = [
  ["맵 이름", "대체 맵 코드", "qualifying %", "Tier Pool"],
  ["xStep", 77238470, "20%", 2],
  ["Clubstep", 77238488, 25, 3],
];
context.ensureRankedMapColumns_();
assert.equal(mapSheet.getLastColumn(), 4);
const minimalMaps = JSON.parse(JSON.stringify(context.readRankedMapsFromCorumSheet_()));
assert.equal(minimalMaps.length, 2);
assert.deepEqual(minimalMaps[0], {levelId:"77238470",canonicalLevelId:"77238470",alternateLevelId:"77238470",
  playableLevelId:"77238470",title:"xStep",creator:"",difficulty:"",length:"",pool:2,qualifyingPercent:20,active:true});
assert.equal(minimalMaps[1].qualifyingPercent, 25);
mapSheet.rows = fullMapRows;

const health = JSON.parse(context.doGet({ parameter: { action: "health" } }).text);
assert.equal(health.service, "Corum Integration API");
assert.equal(health.version, context.CORUM_API_VERSION);


const divisionValues=structuredClone(tierRows);
const legacyRows=[[...context.CORUM_RANKED_TIER_HEADERS],
 ['RED',0,300,2,3,3,1],['AQUA',300,600,3,4,3,2],
 ['BRONZE',600,900,4,5,5,3],['SILVER',900,1200,5,6,5,3],['GOLD',1200,'',6,6,0,0]];
spreadsheet.getSheetByName('Ranked Tiers').rows=structuredClone(legacyRows);
context.migrateRankedTierDivisions_(spreadsheet,spreadsheet.getSheetByName('Ranked Tiers'));
const upgraded=JSON.parse(JSON.stringify(context.readRankedTierBands_(spreadsheet.getSheetByName('Ranked Tiers'))));
assert.equal(upgraded.length,13);
assert.deepEqual(upgraded.slice(0,3).map(b=>[b.tier,b.minInclusive,b.maxExclusive,b.mainPool,b.deathmatchPool,b.promotionGames,b.promotionWinsRequired]),
 [['RED3',0,100,2,3,0,0],['RED2',100,200,2,3,0,0],['RED1',200,300,2,3,3,1]]);
assert.deepEqual(JSON.parse(JSON.stringify(spreadsheet.getSheetByName('Ranked Tiers before beta69').rows)),legacyRows);
const preserved=structuredClone(spreadsheet.getSheetByName('Ranked Tiers').rows);
context.migrateRankedTierDivisions_(spreadsheet,spreadsheet.getSheetByName('Ranked Tiers'));
assert.deepEqual(spreadsheet.getSheetByName('Ranked Tiers').rows,preserved);
spreadsheet.getSheetByName('Ranked Tiers').rows=divisionValues;

console.log("Ranked sheet2 routing, dynamic Main Pool/CSMP seeds, alternate IDs, fail-closed config, and legacy health routing passed.");

