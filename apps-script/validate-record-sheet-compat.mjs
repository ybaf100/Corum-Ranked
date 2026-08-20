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

  getDisplayValues() {
    return Array.from({ length: this.rowCount }, (_, rowOffset) =>
      Array.from({ length: this.columnCount }, (_, columnOffset) =>
        String(
          this.sheet.rows[this.row - 1 + rowOffset]?.[
            this.column - 1 + columnOffset
          ] ?? "",
        ),
      ),
    );
  }

  getValues() {
    return Array.from({ length: this.rowCount }, (_, rowOffset) =>
      Array.from({ length: this.columnCount }, (_, columnOffset) =>
        this.sheet.rows[this.row - 1 + rowOffset]?.[
          this.column - 1 + columnOffset
        ] ?? "",
      ),
    );
  }

  setValues(values) {
    values.forEach((rowValues, rowOffset) => {
      const rowIndex = this.row - 1 + rowOffset;
      if (!this.sheet.rows[rowIndex]) this.sheet.rows[rowIndex] = [];

      rowValues.forEach((value, columnOffset) => {
        this.sheet.rows[rowIndex][this.column - 1 + columnOffset] = value;
      });
    });
    return this;
  }

  setValue(value) {
    return this.setValues([[value]]);
  }
}

let nextMockSheetId = 1000;

class MockSheet {
  constructor(name, rows) {
    this.name = name;
    this.rows = rows.map((row) => [...row]);
    this.sheetId = nextMockSheetId++;
  }

  getName() {
    return this.name;
  }

  getSheetId() {
    return this.sheetId;
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
      Math.max(this.getLastRow(), 1),
      Math.max(this.getLastColumn(), 1),
    );
  }

  getRange(row, column, rowCount = 1, columnCount = 1) {
    return new MockRange(this, row, column, rowCount, columnCount);
  }

  appendRow(row) {
    this.rows.push([...row]);
  }

  insertColumnAfter(afterPosition) {
    this.rows.forEach((row) => row.splice(afterPosition, 0, ""));
    return this;
  }

  setFrozenRows() {}
}

class MockSpreadsheet {
  constructor(sheets) {
    this.sheets = sheets;
  }

  getSheetByName(name) {
    return this.sheets.find((sheet) => sheet.getName() === name) || null;
  }

  getSheets() {
    return [...this.sheets];
  }

  insertSheet(name) {
    const sheet = new MockSheet(name, []);
    this.sheets.push(sheet);
    return sheet;
  }
}

const clearHeaders = [
  "레코드 ID",
  "맵 코드",
  "GD 계정 ID",
  "플레이어",
  "최고 기록(%)",
  "클리어 시각",
  "시도 횟수",
  "점프",
  "플레이 시간(ms)",
  "플랫폼",
  "상태",
  "증거",
  "모드 버전",
  "게임 버전",
  "Geode 버전",
  "클라이언트 시각",
  "최초 등록 기록(%)",
  "최초 등록 순위",
  "최초 등록 최소 기록",
  "최초 등록 점수",
  "점수 공식 버전",
  "점수 확정 시각",
];

const spreadsheet = new MockSpreadsheet([
  new MockSheet("sheet1", [
    [
      "순위",
      "맵 제목",
      "Rating",
      "맵 길이",
      "맵 코드",
      "제작자",
      "Verifier",
      "최소 등록 가능 기록",
      "대체 맵 코드",
    ],
    [
      "40",
      "NAVOTI",
      "10",
      "Medium",
      "92205035",
      "songman33",
      "songman33",
      "100",
      "92205035",
    ],
    [
      "6",
      "SNAPSHOT TEST",
      "18",
      "Long",
      "12345678",
      "hwanhee1",
      "hwanhee1",
      "60",
      "12345679",
    ],
  ]),
  new MockSheet("Records", [
    clearHeaders,
    [
      "canonical-record",
      "92205035",
      "11639807",
      "hwanhee1",
      "100",
      "2026-07-28T09:39:20.681Z",
      "170",
      "823",
      "0",
      "Windows",
      "unverified",
      "",
      "v0.2.10",
      "2.2081",
      "v5.8.2",
      "2026-07-28T09:39:15.886Z",
    ],
  ]),
  new MockSheet("CorumPlayers", [
    ["GD 계정 ID", "플레이어", "토큰 해시", "활성", "생성 시각"],
    [
      "11639807",
      "hwanhee1",
      "legacy-token-hash",
      "TRUE",
      "2026-07-28T09:39:19.413Z",
    ],
  ]),
  new MockSheet("Corum Client Versions", [
    [
      "플랫폼",
      "최소 지원 버전",
      "최신 버전",
      "업데이트 URL",
      "버전 검사 활성",
    ],
    [
      "Windows",
      "v1.0.0",
      "v1.0.1",
      "https://github.com/ybaf100/Corum-integration/releases/latest",
      true,
    ],
    [
      "Android",
      "v0.2.40",
      "v0.2.40",
      "https://github.com/ybaf100/Corum-integration/releases/latest",
      true,
    ],
  ]),
  new MockSheet("CorumClears", [
    clearHeaders,
    [
      "canonical-record",
      "92205035",
      "11639807",
      "hwanhee1",
      "100",
      "2026-07-28T09:39:20.681Z",
      "170",
      "823",
      "0",
      "Windows",
      "unverified",
      "",
      "v0.2.10",
      "2.2081",
      "v5.8.2",
      "2026-07-28T09:39:15.886Z",
    ],
    [
      "legacy-record",
      "92205035",
      "200",
      "LegacyPlayer",
      "100",
      "2026-07-29T09:39:20.681Z",
      "25",
      "100",
      "30000",
      "Windows",
      "verified",
      "",
      "v0.2.9",
      "2.2081",
      "v5.8.2",
      "2026-07-29T09:39:15.886Z",
    ],
  ]),
]);

let generatedUuidCount = 0;
const scriptProperties = new Map([["MAPS_SHEET_NAME", "sheet1"]]);
const mockEvidenceFolder = {
  getId() {
    return "mock-evidence-folder";
  },
  getName() {
    return "Corum Clear Evidence";
  },
  createFile(blob) {
    return {
      getId() {
        return `mock-file-${generatedUuidCount}`;
      },
      getUrl() {
        return `https://drive.google.com/file/d/mock-file-${generatedUuidCount}/view`;
      },
    };
  },
};

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
        getProperty(name) {
          return scriptProperties.get(name) || "";
        },
        setProperty(name, value) {
          scriptProperties.set(name, String(value));
        },
      };
    },
  },
  DriveApp: {
    createFolder() {
      return mockEvidenceFolder;
    },
    getFolderById() {
      return mockEvidenceFolder;
    },
  },
  SpreadsheetApp: {
    getActiveSpreadsheet() {
      return spreadsheet;
    },
  },
  LockService: {
    getScriptLock() {
      return {
        waitLock() {},
        releaseLock() {},
      };
    },
  },
  Utilities: {
    getUuid() {
      generatedUuidCount += 1;
      return generatedUuidCount === 1
        ? "generated-record"
        : `generated-record-${generatedUuidCount}`;
    },
    base64Decode(value) {
      return [...Buffer.from(value, "base64")];
    },
    newBlob(bytes, mimeType, name) {
      return { bytes, mimeType, name };
    },
  },
};

vm.createContext(context);
vm.runInContext(
  fs.readFileSync(new URL("./Code.gs", import.meta.url), "utf8"),
  context,
);

assert.equal(context.CORUM_SHEETS.clears, "Records");
context.setupCorumIntegration();

const recordsSheet = spreadsheet.getSheetByName("Records");
for (const requiredHeader of [
  "점수 반영 기록(%)",
  "점수 반영 순위",
  "점수 반영 최소 기록",
  "확정 점수",
  "점수 공식 버전",
  "점수 확정 시각",
]) {
  assert.ok(recordsSheet.rows[0].includes(requiredHeader));
}
for (const removedHeader of [
  "무결성 판정",
  "의심 기록",
  "해킹 기록",
  "검출 항목",
  "관찰 모드",
  "Cheat API AREDL",
  "검증 경로",
  "무결성 규칙",
  "무결성 스키마",
  "무결성 검사 시각",
  "무결성 증표",
  "증표 시각",
  "기록 출처",
]) {
  assert.equal(recordsSheet.rows[0].includes(removedHeader), false);
}
assert.equal(spreadsheet.getSheetByName("IntegrityFlags"), null);
for (const requiredSheet of [
  "CorumPlayers",
  "Records",
  "CorumPublicClears",
  "ClearEvidence",
  "CSMP Tiers",
  "CorumVerifierRecords",
  "Corum Client Versions",
]) {
  assert.ok(spreadsheet.getSheetByName(requiredSheet));
}
assert.ok(spreadsheet.getSheetByName("ClearEvidence"));
assert.ok(recordsSheet.rows[0].includes("엔드스크린 증거 ID"));
assert.ok(recordsSheet.rows[0].includes("엔드스크린 파일 URL"));
assert.ok(recordsSheet.rows[0].includes("사용 모드 목록"));
assert.ok(
  spreadsheet.getSheetByName("CorumPublicClears").rows[0].includes("엔드스크린 증거 ID"),
);
assert.ok(
  spreadsheet.getSheetByName("CorumPublicClears").rows[0].includes("엔드스크린 파일 URL"),
);
assert.equal(
  spreadsheet.getSheetByName("CorumPublicClears").rows[0].includes("사용 모드 목록"),
  false,
);
assert.equal(recordsSheet.rows[0][2], "맵 제목");
assert.equal(
  recordsSheet.rows.find((row) => String(row[0]) === "canonical-record")[2],
  "NAVOTI",
);

const clearPayload = JSON.parse(
  context.getClearsResponse_("92205035", "200").text,
);
assert.equal(clearPayload.ok, true);
assert.ok(clearPayload.records.length >= 2);
assert.ok(clearPayload.records.some((record) => record.recordId === "canonical-record"));
assert.ok(clearPayload.records.some((record) => record.recordId === "legacy-record"));

const scorePayload = JSON.parse(context.getScoresResponse_("500").text);
assert.equal(scorePayload.ok, true);
assert.ok(scorePayload.players.length >= 2);
assert.ok(scorePayload.players.some((player) => player.player === "LegacyPlayer"));
assert.ok(scorePayload.players.some((player) => player.player === "hwanhee1"));
const hwanhee = scorePayload.players.find((player) => player.player === "hwanhee1");
assert.equal(hwanhee.accountId, "11639807");
assert.ok(hwanhee.records.length >= 1);
const hwanheeNavoti = hwanhee.records.find(
  (record) => record.levelId === "92205035",
);
assert.ok(hwanheeNavoti);
assert.equal(hwanheeNavoti.title, "NAVOTI");
assert.equal(hwanheeNavoti.percent, 100);
assert.equal(hwanheeNavoti.minimumRecord, 100);
assert.ok(hwanhee.bestRecord);
const migratedScore = hwanheeNavoti.score;
assert.ok(migratedScore > 0);

const playerRecordsPayload = JSON.parse(
  context.getScoresResponse_("1", "11639807").text,
);
assert.equal(playerRecordsPayload.ok, true);
assert.equal(playerRecordsPayload.players.length, 1);
assert.equal(playerRecordsPayload.players[0].accountId, "11639807");
assert.equal(playerRecordsPayload.players[0].player, "hwanhee1");
const playerRecordsGetPayload = JSON.parse(
  context.doGet({
    parameter: {
      action: "playerRecords",
      gdAccountId: "11639807",
    },
  }).text,
);
assert.equal(playerRecordsGetPayload.players.length, 1);
assert.equal(playerRecordsGetPayload.players[0].accountId, "11639807");

const listPayload = JSON.parse(
  context.doGet({ parameter: { action: "list" } }).text,
);
assert.equal(listPayload.ok, true);
assert.match(listPayload.evidenceGeneration, /^\d+$/);
assert.equal(listPayload.clientPolicy.windows.minimumSupportedVersion, "v1.0.0");
assert.equal(listPayload.clientPolicy.windows.latestVersion, "v1.0.2");
assert.equal(listPayload.clientPolicy.android.minimumSupportedVersion, "v1.0.0");
assert.equal(listPayload.clientPolicy.android.latestVersion, "v1.0.2");
assert.equal(listPayload.clientPolicy.ios.minimumSupportedVersion, "v1.0.2");
assert.equal(listPayload.clientPolicy.ios.latestVersion, "v1.0.2");
assert.equal(listPayload.clientPolicy.windows.enforcementEnabled, true);
const originalEvidenceGeneration = listPayload.evidenceGeneration;

assert.equal(
  context.compareSemanticVersions_(
    context.parseSemanticVersion_("v0.2.40"),
    context.parseSemanticVersion_("v0.2.9"),
  ),
  1,
);
assert.equal(
  context.compareSemanticVersions_(
    context.parseSemanticVersion_("v1.0.0-beta.1"),
    context.parseSemanticVersion_("v1.0.0"),
  ),
  -1,
);
assert.equal(context.clientVersionRejection_({
  platform: "Android64",
  modVersion: "v1.0.0",
}), null);
assert.equal(context.clientVersionRejection_({
  platform: "iOS",
  modVersion: "v1.0.2",
}), null);
assert.equal(context.clientVersionRejection_({
  platform: "iPhone",
  modVersion: "v1.0.2",
}), null);
assert.equal(context.clientVersionRejection_({
  platform: "iPadOS",
  modVersion: "v1.0.2",
}), null);
assert.equal(
  JSON.parse(context.clientVersionRejection_({
    platform: "iOS",
    modVersion: "v1.0.1",
  }).text).error.code,
  "CLIENT_OUTDATED",
);
assert.equal(
  JSON.parse(context.clientVersionRejection_({
    platform: "iOS",
    modVersion: "v1.0.0",
  }).text).error.code,
  "CLIENT_OUTDATED",
);
assert.equal(
  JSON.parse(context.clientVersionRejection_({
    platform: "Android32",
    modVersion: "v0.2.40",
  }).text).error.code,
  "CLIENT_OUTDATED",
);
assert.equal(
  JSON.parse(context.clientVersionRejection_({
    platform: "Windows",
    modVersion: "",
  }).text).error.code,
  "CLIENT_VERSION_REQUIRED",
);

const mapsSheet = spreadsheet.getSheetByName("sheet1");
const mapList = context.readMaps_();
assert.equal(
  mapList.find((map) => map.levelId === "92205035").alternateLevelId,
  "",
);
assert.equal(
  mapList.find((map) => map.levelId === "12345678").alternateLevelId,
  "12345679",
);
assert.equal(
  JSON.parse(context.getMapResponse_("12345679", "").text).map.levelId,
  "12345678",
);
mapsSheet.rows[1][0] = "1";
const scoreAfterLegacyRankChange = JSON.parse(
  context.getScoresResponse_("500").text,
);
assert.equal(
  scoreAfterLegacyRankChange.players.find(
    (player) => player.player === "hwanhee1",
  ).records.find((record) => record.levelId === "92205035").score,
  migratedScore,
);

const submissionBody = {
  action: "record",
  levelId: 12345679,
  gdAccountId: 30000001,
  gdUsername: "NewPlayer",
  percent: 60,
  attempts: 12,
  jumps: 34,
  playTimeMs: 56000,
  platform: "Windows",
  modVersion: "v1.0.0",
  gameVersion: "2.2081",
  geodeVersion: "v5.8.2",
  loadedMods: [
    "geode.node-ids@v1.23.3",
    "thesillydoggo.qolmod@v4.6.1",
    "=invalid-sheet-formula",
    "geode.node-ids@v1.23.3",
  ],
  clientTimestamp: Date.parse("2026-07-30T12:00:00.000Z"),
};

const outdatedSubmissionPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        ...submissionBody,
        modVersion: "v0.9.9",
      }),
    },
  }).text,
);
assert.equal(outdatedSubmissionPayload.ok, false);
assert.equal(outdatedSubmissionPayload.error.code, "CLIENT_OUTDATED");
assert.equal(outdatedSubmissionPayload.error.minimumSupportedVersion, "v1.0.0");

const createdPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify(submissionBody),
    },
  }).text,
);
assert.equal(createdPayload.ok, true);
assert.equal(createdPayload.created, true);
assert.equal(createdPayload.playerRegistered, true);
assert.equal(createdPayload.record.initialPercent, 60);
assert.equal(createdPayload.record.registeredRank, 6);
assert.equal(createdPayload.record.registeredMinimumRecord, 60);
assert.equal(createdPayload.record.scoredPercent, 60);
assert.equal(createdPayload.record.scoredRank, 6);
assert.equal(createdPayload.record.scoredMinimumRecord, 60);
assert.equal(createdPayload.record.score, 20);
assert.equal(createdPayload.record.scoringVersion, "corum-v1");
assert.equal(Object.hasOwn(createdPayload.record, "loadedMods"), false);

const playersSheet = spreadsheet.getSheetByName("CorumPlayers");
assert.ok(playersSheet);
assert.ok(playersSheet.rows[0].includes("최근 활동 시각"));
assert.ok(playersSheet.rows[0].includes("토큰 해시"));
const newPlayerRow = playersSheet.rows.find(
  (row) => String(row[0]) === String(submissionBody.gdAccountId),
);
assert.ok(newPlayerRow);
assert.equal(newPlayerRow[1], "NewPlayer");
assert.equal(newPlayerRow[playersSheet.rows[0].indexOf("활성")], true);

const generatedRecord = recordsSheet.rows.find(
  (row) => String(row[0]) === "generated-record",
);
assert.ok(generatedRecord);
assert.equal(String(generatedRecord[1]), "12345678");
assert.equal(String(generatedRecord[2]), "SNAPSHOT TEST");
assert.equal(
  String(generatedRecord[recordsSheet.rows[0].indexOf("GD 계정 ID")]),
  "30000001",
);
assert.equal(
  generatedRecord[recordsSheet.rows[0].indexOf("점수 반영 기록(%)")],
  60,
);
assert.equal(
  generatedRecord[recordsSheet.rows[0].indexOf("점수 반영 순위")],
  6,
);
assert.equal(
  generatedRecord[recordsSheet.rows[0].indexOf("확정 점수")],
  20,
);
assert.equal(
  generatedRecord[recordsSheet.rows[0].indexOf("사용 모드 목록")],
  "geode.node-ids@v1.23.3, thesillydoggo.qolmod@v4.6.1",
);

const unchangedPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        ...submissionBody,
        loadedMods: ["example.overlay@v1.0.0"],
      }),
    },
  }).text,
);
assert.equal(unchangedPayload.ok, true);
assert.equal(unchangedPayload.unchanged, true);
assert.equal(unchangedPayload.playerRegistered, false);
assert.equal(unchangedPayload.record.score, 20);
assert.equal(
  generatedRecord[recordsSheet.rows[0].indexOf("사용 모드 목록")],
  "example.overlay@v1.0.0",
);

const legacyAlternateRecord = [...generatedRecord];
legacyAlternateRecord[0] = "legacy-alternate-record";
legacyAlternateRecord[1] = "12345679";
legacyAlternateRecord[4] = "55";
recordsSheet.appendRow(legacyAlternateRecord);

const alternateClearsPayload = JSON.parse(
  context.getClearsResponse_("12345679", "200").text,
);
assert.equal(alternateClearsPayload.levelId, "12345678");
assert.equal(
  alternateClearsPayload.records.filter(
    (record) => record.player === "NewPlayer",
  ).length,
  1,
);
assert.equal(
  JSON.parse(context.getScoresResponse_("500").text).players.find(
    (player) => player.player === "NewPlayer",
  ).records.length,
  1,
);

mapsSheet.rows[2][0] = "1";
mapsSheet.rows[2][7] = "100";
const updatedPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        ...submissionBody,
        percent: 100,
      }),
    },
  }).text,
);
assert.equal(updatedPayload.ok, true);
assert.equal(updatedPayload.updated, true);
assert.equal(updatedPayload.record.percent, 100);
assert.equal(updatedPayload.record.scoredPercent, 100);
assert.equal(updatedPayload.record.scoredRank, 1);
assert.equal(updatedPayload.record.scoredMinimumRecord, 100);
assert.equal(updatedPayload.record.initialPercent, 100);
assert.equal(updatedPayload.record.registeredRank, 1);
assert.equal(updatedPayload.record.registeredMinimumRecord, 100);
assert.equal(updatedPayload.record.score, 350);
const updatedGeneratedRecord = recordsSheet.rows.find(
  (row) => String(row[0]) === "generated-record",
);
assert.ok(updatedGeneratedRecord);

const mapPayloadWithPlayer = JSON.parse(
  context.getMapResponse_("12345678", "30000001").text,
);
assert.equal(mapPayloadWithPlayer.ok, true);
assert.equal(mapPayloadWithPlayer.map.rank, 1);
assert.equal(mapPayloadWithPlayer.playerRecord.percent, 100);
assert.equal(mapPayloadWithPlayer.playerRecord.scoredPercent, 100);
assert.equal(mapPayloadWithPlayer.playerRecord.scoredRank, 1);
assert.equal(mapPayloadWithPlayer.playerRecord.initialPercent, 100);
assert.equal(mapPayloadWithPlayer.playerRecord.registeredRank, 1);
assert.equal(mapPayloadWithPlayer.playerRecord.score, 350);

const frozenScorePayload = JSON.parse(context.getScoresResponse_("500").text);
assert.equal(frozenScorePayload.scorePolicy, "best-improvement-frozen");
const newPlayerScore = frozenScorePayload.players.find(
  (player) => player.player === "NewPlayer",
);
assert.equal(newPlayerScore.score, 350);
assert.equal(newPlayerScore.records[0].percent, 100);
assert.equal(newPlayerScore.records[0].scoredPercent, 100);
assert.equal(newPlayerScore.records[0].scoredRank, 1);
assert.equal(newPlayerScore.records[0].initialPercent, 100);
assert.equal(newPlayerScore.records[0].rank, 1);
assert.equal(newPlayerScore.records[0].currentRank, 1);
assert.equal(newPlayerScore.records[0].minimumRecord, 100);

assert.equal(
  context.setClearStatus(
    "generated-record",
    "verified",
    "https://example.com/proof",
  ),
  true,
);
const verifiedMapPayload = JSON.parse(
  context.getMapResponse_("12345678", "30000001").text,
);
assert.equal(verifiedMapPayload.playerRecord.status, "verified");
assert.equal(verifiedMapPayload.playerRecord.score, 350);

const playerHeaders = playersSheet.rows[0];
playersSheet.appendRow(
  playerHeaders.map((header) => {
    if (header === "GD 계정 ID") return 40000001;
    if (header === "플레이어") return "BlockedPlayer";
    if (header === "활성") return false;
    if (header === "생성 시각") return "2026-07-30T12:00:00.000Z";
    return "";
  }),
);

const blockedPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        ...submissionBody,
        gdAccountId: 40000001,
        gdUsername: "BlockedPlayer",
        percent: 100,
      }),
    },
  }).text,
);
assert.equal(blockedPayload.ok, false);
assert.equal(blockedPayload.error.code, "PLAYER_DISABLED");

mapsSheet.rows.push([
  "2",
  "BATCH TEST",
  "17",
  "Long",
  "87654321",
  "hwanhee1",
  "hwanhee1",
  "50",
  "87654322",
]);

const batchPayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        action: "batchRecords",
        gdAccountId: 50000001,
        gdUsername: "BatchPlayer",
        platform: "Windows",
        modVersion: "v1.0.0",
        gameVersion: "2.2081",
        geodeVersion: "v5.8.2",
        loadedMods: [
          "geode.node-ids@v1.23.3",
          "weebify.separate_dual_icons@v1.2.0",
        ],
        clientTimestamp: Date.parse("2026-07-31T12:00:00.000Z"),
        records: [
          {
            levelId: 12345678,
            percent: 100,
            attempts: 20,
            jumps: 40,
            playTimeMs: 0,
          },
          {
            levelId: 87654322,
            percent: 75,
            attempts: 30,
            jumps: 50,
            playTimeMs: 0,
          },
          {
            levelId: 99999999,
            percent: 100,
            attempts: 1,
            jumps: 1,
            playTimeMs: 0,
          },
        ],
      }),
    },
  }).text,
);

assert.equal(context.CORUM_API_VERSION, "2.23");
assert.equal(batchPayload.ok, true);
assert.equal(batchPayload.batch, true);
assert.equal(batchPayload.requested, 3);
assert.equal(batchPayload.succeeded, 2);
assert.equal(batchPayload.failed, 1);
assert.equal(batchPayload.playerRegistered, true);
assert.equal(batchPayload.results[0].created, true);
assert.equal(batchPayload.results[0].record.percent, 100);
assert.equal(batchPayload.results[1].created, true);
assert.equal(batchPayload.results[1].levelId, "87654321");
assert.equal(batchPayload.results[1].record.percent, 75);
assert.equal(batchPayload.results[1].record.scoredRank, 2);
assert.equal(batchPayload.results[1].record.scoredMinimumRecord, 50);
assert.equal(batchPayload.results[2].ok, false);
assert.equal(batchPayload.results[2].error.code, "MAP_NOT_FOUND");

const batchPlayerRows = recordsSheet.rows.filter(
  (row) =>
    String(row[recordsSheet.rows[0].indexOf("GD 계정 ID")]) === "50000001",
);
const normalBatchRow = batchPlayerRows.find((row) => String(row[1]) === "12345678");
const unattestedBatchRow = batchPlayerRows.find((row) => String(row[1]) === "87654321");
assert.equal(String(normalBatchRow[2]), "SNAPSHOT TEST");
assert.equal(String(unattestedBatchRow[2]), "BATCH TEST");
assert.equal(
  normalBatchRow[recordsSheet.rows[0].indexOf("사용 모드 목록")],
  "geode.node-ids@v1.23.3, weebify.separate_dual_icons@v1.2.0",
);
assert.equal(
  unattestedBatchRow[recordsSheet.rows[0].indexOf("사용 모드 목록")],
  "geode.node-ids@v1.23.3, weebify.separate_dual_icons@v1.2.0",
);

const batchPlayerScore = JSON.parse(
  context.getScoresResponse_("500").text,
).players.find((player) => player.player === "BatchPlayer");
assert.ok(batchPlayerScore);
assert.equal(batchPlayerScore.records.length, 2);
assert.equal(
  batchPlayerScore.records.find((record) => record.levelId === "87654321").percent,
  75,
);

const tinyPngHeader = Buffer.from([
  0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a,
  0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
]).toString("base64");
const evidencePayload = JSON.parse(
  context.doPost({
    postData: {
      contents: JSON.stringify({
        action: "evidence",
        levelId: 12345678,
        gdAccountId: 50000001,
        gdUsername: "BatchPlayer",
        mimeType: "image/png",
        imageBase64: tinyPngHeader,
        width: 1,
        height: 1,
        platform: "Windows",
        modVersion: "v0.2.31",
        gameVersion: "2.2081",
        geodeVersion: "v5.8.2",
        loadedMods: [
          "geode.node-ids@v1.23.3",
          "thesillydoggo.qolmod@v4.6.1",
        ],
        clientTimestamp: Date.parse("2026-08-07T12:00:00.000Z"),
      }),
    },
  }).text,
);
assert.equal(evidencePayload.ok, true);
assert.equal(evidencePayload.evidence.width, 1);
assert.equal(evidencePayload.evidence.height, 1);
assert.ok(evidencePayload.evidence.linkedRecordId);
const evidenceSheet = spreadsheet.getSheetByName("ClearEvidence");
assert.equal(evidenceSheet.rows.length, 2);
assert.equal(
  evidenceSheet.rows[1][evidenceSheet.rows[0].indexOf("사용 모드 목록")],
  "geode.node-ids@v1.23.3, thesillydoggo.qolmod@v4.6.1",
);
const linkedEvidenceIdColumn = recordsSheet.rows[0].indexOf("엔드스크린 증거 ID");
const linkedEvidenceUrlColumn = recordsSheet.rows[0].indexOf("엔드스크린 파일 URL");
const recordProofColumn = recordsSheet.rows[0].indexOf("증거");
assert.equal(
  normalBatchRow[linkedEvidenceIdColumn],
  evidencePayload.evidence.id,
);
assert.match(String(normalBatchRow[linkedEvidenceUrlColumn]), /^https:\/\/drive\.google\.com\//);
assert.equal(normalBatchRow[recordProofColumn], normalBatchRow[linkedEvidenceUrlColumn]);

const publicSheet = spreadsheet.getSheetByName("CorumPublicClears");
const publicRecordIdColumn = publicSheet.rows[0].indexOf("레코드 ID");
const publicEvidenceIdColumn = publicSheet.rows[0].indexOf("엔드스크린 증거 ID");
const publicEvidenceUrlColumn = publicSheet.rows[0].indexOf("엔드스크린 파일 URL");
const publicProofColumn = publicSheet.rows[0].indexOf("증거");
let normalPublicRow = publicSheet.rows.find(
  (row) => String(row[publicRecordIdColumn]) === String(evidencePayload.evidence.linkedRecordId),
);
assert.equal(normalPublicRow[publicEvidenceIdColumn], evidencePayload.evidence.id);
assert.equal(normalPublicRow[publicProofColumn], normalPublicRow[publicEvidenceUrlColumn]);

// Regression: older API versions could leave the PNG only in ClearEvidence.
// setupCorumIntegration() must repair both private and public record rows.
normalBatchRow[linkedEvidenceIdColumn] = "";
normalBatchRow[linkedEvidenceUrlColumn] = "";
normalBatchRow[recordProofColumn] = "";
normalPublicRow[publicEvidenceIdColumn] = "";
normalPublicRow[publicEvidenceUrlColumn] = "";
normalPublicRow[publicProofColumn] = "";
context.setupCorumIntegration();
assert.equal(normalBatchRow[linkedEvidenceIdColumn], evidencePayload.evidence.id);
assert.match(String(normalBatchRow[linkedEvidenceUrlColumn]), /^https:\/\/drive\.google\.com\//);
normalPublicRow = publicSheet.rows.find(
  (row) => String(row[publicRecordIdColumn]) === String(evidencePayload.evidence.linkedRecordId),
);
assert.equal(normalPublicRow[publicEvidenceIdColumn], evidencePayload.evidence.id);
assert.equal(normalPublicRow[publicProofColumn], normalPublicRow[publicEvidenceUrlColumn]);

// Clean-reset smoke test: keep only the maps sheet, then rebuild integration tabs.
spreadsheet.sheets = [new MockSheet("sheet1", mapsSheet.rows)];
context.setupCorumIntegration();
assert.deepEqual(
  spreadsheet
    .getSheets()
    .map((sheet) => sheet.getName())
    .sort(),
  [
    "CSMP Tiers",
    "ClearEvidence",
    "CorumPlayers",
    "CorumPublicClears",
    "CorumVerifierRecords",
    "Corum Client Versions",
    "Records",
    "sheet1",
  ].sort(),
);
const resetListPayload = JSON.parse(
  context.doGet({ parameter: { action: "list" } }).text,
);
assert.notEqual(
  resetListPayload.evidenceGeneration,
  originalEvidenceGeneration,
);

console.log(
  "Clean record schema, scoring, alternate-map normalization, batch submission, and end-screen evidence validation passed.",
);
