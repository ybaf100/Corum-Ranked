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
          pool === 5 && index === 1 ? "987654321" : "",
          "Creator",
          "Verifier",
          50,
          "Gold",
        ]);
      }
    }
    this.sheets = [new MockSheet("sheet1", [headers, ...maps])];
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

const mapSheet = spreadsheet.getSheetByName("sheet1");
const poolColumn = mapSheet.rows[0].indexOf("Ranked Pool (1~6)");
const qualifyingColumn = mapSheet.rows[0].indexOf("Qualifying %");
assert.ok(poolColumn >= 0);
assert.ok(qualifyingColumn >= 0);
for (let index = 1; index < mapSheet.rows.length; index += 1) {
  mapSheet.rows[index][poolColumn] = Math.floor((index - 1) / 5) + 1;
  mapSheet.rows[index][qualifyingColumn] = index === 1 ? "35%" : 50;
}
context.setupCorumRankedConfig();
assert.equal(mapSheet.rows[0].filter((value) => value === "Ranked Pool (1~6)").length, 1);
assert.equal(mapSheet.rows[0].filter((value) => value === "Qualifying %").length, 1);

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
  row[1] = tierRanges[row[0]][0];
  row[2] = tierRanges[row[0]][1];
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
assert.equal(complete.data.maps[0].playableLevelId, "11001");
assert.equal(complete.data.maps[0].qualifyingPercent, 35);
const alternate = complete.data.maps.find((map) => map.canonicalLevelId === "51001");
assert.equal(alternate.alternateLevelId, "987654321");
assert.equal(alternate.playableLevelId, "987654321");
assert.equal(alternate.levelId, "987654321");
assert.equal(complete.data.operational.rules.roundSeconds, 180);
assert.equal(complete.data.operational.cbf.requiredSettings["soft-toggle"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["click-on-steps"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["physics-bypass"], false);
assert.equal(complete.data.operational.generation, complete.data.generation);
assert.equal(complete.data.client.audio.enabled, false);
assert.deepEqual(complete.data.client.audio.resources, []);
assert.equal(complete.data.client.audio.fadeInSeconds, 0.8);
assert.equal(complete.data.client.audio.fadeOutSeconds, 0.6);
assert.equal(complete.data.client.ui.fadeInSeconds, 0.24);
assert.equal(complete.data.client.ui.fadeOutSeconds, 0.18);

setConfig("rankedAudioMenuSongId", 123456);
setConfig("rankedAudioMenuStartSeconds", 42.5);
setConfig("rankedAudioMatchSongId", 234567);
setConfig("rankedAudioMatchStartSeconds", 10);
setConfig("rankedAudioEnabled", true);
const withResources = responseJson();
assert.equal(withResources.data.validation.valid, true, withResources.data.validation.errors.join("\n"));
assert.equal(withResources.data.client.audio.enabled, true);
assert.deepEqual(
  withResources.data.client.audio.resources.map((resource) => [resource.key, resource.songId, resource.startSeconds]),
  [["menu", 123456, 42.5], ["match", 234567, 10]],
);

const repeated = responseJson();
assert.equal(repeated.data.generation, withResources.data.generation);
mapSheet.rows[1][qualifyingColumn] = 55;
const changed = responseJson();
assert.notEqual(changed.data.generation, withResources.data.generation);

mapSheet.rows[2][poolColumn] = "";
const excluded = responseJson();
assert.equal(excluded.data.maps.length, 29);

const health = JSON.parse(context.doGet({ parameter: { action: "health" } }).text);
assert.equal(health.service, "Corum Integration API");
assert.equal(health.version, context.CORUM_API_VERSION);

console.log("Ranked map columns on sheet1, alternate/playable IDs, fail-closed config, and legacy health routing passed.");
