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
    this.sheets = [
      new MockSheet("sheet1", [
        ["순위", "맵 제목", "Rating", "맵 길이", "맵 코드", "대체 맵 코드", "제작자", "Verifier"],
        [1, "Map 1-1", 10, "Long", "1001", "91001", "Creator", "Verifier"],
      ]),
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
  "Ranked Pool",
  "Ranked Tiers",
  "Ranked CSMP Seed",
  "Ranked Allowed Mods",
  "Ranked Config",
]) {
  assert.ok(spreadsheet.getSheetByName(name), `Missing ${name}`);
}

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

const poolSheet = spreadsheet.getSheetByName("Ranked Pool");
for (let pool = 1; pool <= 6; pool += 1) {
  for (let index = 1; index <= 5; index += 1) {
    const canonicalLevelId = `${pool}${String(index).padStart(3, "0")}`;
    poolSheet.appendRow([
      canonicalLevelId,
      canonicalLevelId,
      pool === 1 && index === 2 ? "invalid" : "",
      `Map ${pool}-${index}`,
      "Creator",
      "Difficulty",
      pool,
      50,
      true,
    ]);
  }
}

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
assert.equal(complete.data.operational.rules.roundSeconds, 180);
assert.equal(complete.data.operational.cbf.requiredSettings["soft-toggle"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["click-on-steps"], false);
assert.equal(complete.data.operational.cbf.requiredSettings["physics-bypass"], false);
assert.equal(complete.data.operational.generation, complete.data.generation);
const alternateMap = complete.data.maps.find((map) => map.canonicalLevelId === "1001");
assert.equal(alternateMap.alternateLevelId, "91001");
assert.equal(Object.prototype.hasOwnProperty.call(alternateMap, "levelId"), false);
assert.equal(
  complete.data.maps.find((map) => map.canonicalLevelId === "1002").alternateLevelId,
  null,
);

const repeated = responseJson();
assert.equal(repeated.data.generation, complete.data.generation);
poolSheet.rows[1][7] = 55;
const changed = responseJson();
assert.notEqual(changed.data.generation, complete.data.generation);

const health = JSON.parse(context.doGet({ parameter: { action: "health" } }).text);
assert.equal(health.service, "Corum Integration API");
assert.equal(health.version, context.CORUM_API_VERSION);

console.log("Ranked config setup, canonical/alternate map export, fail-closed validation, snapshot generation, and legacy health routing passed.");
