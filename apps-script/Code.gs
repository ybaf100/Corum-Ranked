var CORUM_API_VERSION = "2.23";
var CORUM_SCORING_VERSION = "corum-v1";
var CORUM_SCORE_POLICY = "best-improvement-frozen";

var CORUM_SHEETS = Object.freeze({
  clears: "Records",
  publicClears: "CorumPublicClears",
  players: "CorumPlayers",
  csmpTiers: "CSMP Tiers",
  verifierRecords: "CorumVerifierRecords",
  clearEvidence: "ClearEvidence",
  clientVersions: "Corum Client Versions",
});

var CORUM_CLIENT_VERSION_HEADERS = Object.freeze([
  "플랫폼",
  "최소 지원 버전",
  "최신 버전",
  "업데이트 URL",
  "버전 검사 활성",
]);

var CORUM_CLIENT_VERSION_DEFAULTS = Object.freeze([
  Object.freeze([
    "Windows",
    "v1.0.0",
    "v1.0.2",
    "https://github.com/ybaf100/Corum-integration/releases/latest",
    true,
  ]),
  Object.freeze([
    "Android",
    "v1.0.0",
    "v1.0.2",
    "https://github.com/ybaf100/Corum-integration/releases/latest",
    true,
  ]),
  Object.freeze([
    "iOS",
    "v1.0.2",
    "v1.0.2",
    "https://github.com/ybaf100/Corum-integration/releases/latest",
    true,
  ]),
]);

var CORUM_VERIFIER_RECORD_HEADERS = Object.freeze([
  "Verifier 레코드 ID",
  "맵 코드",
  "맵 제목",
  "Verifier",
  "Verifier GD 계정 ID",
  "Verifier 기록(%)",
  "Verifier 등록 당시 순위",
  "Verifier 등록 당시 최소 기록",
  "Verifier 확정 점수",
  "Verifier 점수 공식 버전",
  "Verifier 등록 시각",
]);

var CORUM_CSMP_TIER_HEADERS = Object.freeze([
  "진행 순서",
  "티어명",
  "아이콘 파일명",
  "대표 색상",
  "최소 클리어 맵 수",
]);

var CORUM_CSMP_DEFAULT_TIERS = Object.freeze([
  Object.freeze([1, "Red", "White.png", "#EF4444", "All"]),
  Object.freeze([2, "Aqua", "Aqua.png", "#63E7EC", 5]),
  Object.freeze([3, "Bronze", "Bronze.png", "#C98449", 5]),
  Object.freeze([4, "Silver", "Silver.png", "#D7DBE2", "All"]),
  Object.freeze([5, "Gold", "Gold.png", "#F3D45E", "All"]),
]);

var CORUM_CSMP_DEFAULT_MAP_TIERS = Object.freeze({
  fuselagev: "Red",
  vertexvacancy: "Red",
  navoti: "Red",
  easyacti: "Red",
  magasiga: "Aqua",
  actinum: "Aqua",
  cry: "Aqua",
  requims: "Aqua",
  navotiadventurex: "Aqua",
  variousadventure: "Aqua",
  oatursurix: "Bronze",
  actinumal: "Bronze",
  arfojsweip: "Bronze",
  dismalofdarkness: "Bronze",
  choice: "Bronze",
  fuselaget: "Bronze",
  thedreadful: "Silver",
  lightsx: "Silver",
  translast: "Silver",
  sagittarius: "Silver",
  triniticcircles: "Gold",
  revanism: "Gold",
  nerfthis: "Gold",
  degradex: "Gold",
  scorpius: "Gold",
});

var CORUM_LEGACY_CLEAR_SHEET_NAMES = Object.freeze(["CorumClears"]);

var CORUM_CLEAR_HEADERS = Object.freeze([
  "레코드 ID",
  "맵 코드",
  "맵 제목",
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
  "사용 모드 목록",
  "클라이언트 시각",
  "점수 반영 기록(%)",
  "점수 반영 순위",
  "점수 반영 최소 기록",
  "확정 점수",
  "점수 공식 버전",
  "점수 확정 시각",
  "엔드스크린 증거 ID",
  "엔드스크린 파일 URL",
]);

var CORUM_CLEAR_EVIDENCE_HEADERS = Object.freeze([
  "증거 ID",
  "맵 코드",
  "클리어한 맵 코드",
  "GD 계정 ID",
  "플레이어",
  "캡처 시각",
  "업로드 시각",
  "파일 ID",
  "파일 URL",
  "파일명",
  "MIME",
  "원본 폭",
  "원본 높이",
  "파일 크기(bytes)",
  "모드 버전",
  "게임 버전",
  "Geode 버전",
  "사용 모드 목록",
  "플랫폼",
  "연결 레코드 ID",
]);

var CORUM_PLAYER_HEADERS = Object.freeze([
  "GD 계정 ID",
  "플레이어",
  "가입 상태",
  "활성",
  "생성 시각",
  "최근 활동 시각",
]);

var CORUM_PUBLIC_CLEAR_HEADERS = Object.freeze([
  "레코드 ID",
  "맵 코드",
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
  "점수 반영 기록(%)",
  "점수 반영 순위",
  "점수 반영 최소 기록",
  "확정 점수",
  "점수 공식 버전",
  "점수 확정 시각",
  "엔드스크린 증거 ID",
  "엔드스크린 파일 URL",
]);

var CORUM_SCORE_HEADER_ALIASES = Object.freeze({
  "점수 반영 기록(%)": Object.freeze([
    "점수 반영 기록(%)",
    "최초 등록 기록(%)",
    "최초 등록 기록",
    "Initial Record",
    "Scored Record",
  ]),
  "점수 반영 순위": Object.freeze([
    "점수 반영 순위",
    "최초 등록 순위",
    "등록 당시 순위",
    "Registered Rank",
    "Scored Rank",
  ]),
  "점수 반영 최소 기록": Object.freeze([
    "점수 반영 최소 기록",
    "최초 등록 최소 기록",
    "등록 당시 최소 등록 기록",
    "Registered Minimum",
    "Scored Minimum",
  ]),
  "확정 점수": Object.freeze([
    "확정 점수",
    "최초 등록 점수",
    "Frozen Score",
    "Awarded Score",
  ]),
});

function doGet(event) {
  try {
    var action = String((event && event.parameter && event.parameter.action) || "health")
      .trim()
      .toLowerCase();

    if (action === "health") {
      return json_({
        ok: true,
        service: "Corum Integration API",
        version: CORUM_API_VERSION,
      });
    }

    if (action === "ranked_config") {
      return getCorumRankedConfigResponse_();
    }

    if (action === "map") {
      return getMapResponse_(event.parameter.levelId, event.parameter.gdAccountId);
    }

    if (action === "list") {
      return json_({
        ok: true,
        maps: readMapsWithVerifierSnapshots_(),
        evidenceGeneration: clearEvidenceGeneration_(),
        clientPolicy: readClientVersionPolicies_(),
      });
    }

    if (action === "csmp") {
      return getCsmpResponse_();
    }

    if (action === "clears") {
      return getClearsResponse_(event.parameter.levelId, event.parameter.limit);
    }

    if (action === "scores") {
      return getScoresResponse_(event.parameter.limit);
    }

    if (action === "playerrecords" || action === "player_records") {
      return getScoresResponse_(1, event.parameter.gdAccountId);
    }

    return jsonError_("UNKNOWN_ACTION", "지원하지 않는 요청입니다.");
  } catch (error) {
    console.error(error && error.stack ? error.stack : error);
    return jsonError_("INTERNAL_ERROR", "서버에서 요청을 처리하지 못했습니다.");
  }
}

function doPost(event) {
  try {
    if (!event || !event.postData || !event.postData.contents) {
      return jsonError_("EMPTY_BODY", "요청 본문이 없습니다.");
    }

    var body;
    try {
      body = JSON.parse(event.postData.contents);
    } catch (error) {
      return jsonError_("INVALID_JSON", "JSON 형식이 올바르지 않습니다.");
    }

    var action = String(body.action || "record").trim().toLowerCase();
    if (action === "evidence" || action === "clear_evidence") {
      return submitClearEvidence_(body);
    }

    if (action === "batchrecords" || action === "batch_records") {
      return submitBatchRecords_(body);
    }

    if (action !== "record" && action !== "clear") {
      return jsonError_("UNKNOWN_ACTION", "지원하지 않는 요청입니다.");
    }

    return submitRecord_(body);
  } catch (error) {
    console.error(error && error.stack ? error.stack : error);
    return jsonError_("INTERNAL_ERROR", "서버에서 요청을 처리하지 못했습니다.");
  }
}

/**
 * 최초 설치와 버전 업데이트 후 실행한다.
 * 연동 시트의 누락 열을 추가하고 맵 관리 시트의 관리 열을 준비한다.
 */
function setupCorumIntegration() {
  var spreadsheet = getSpreadsheet_();
  var playersSheet = getOrCreateSheet_(spreadsheet, CORUM_SHEETS.players, CORUM_PLAYER_HEADERS);
  var clearsSheet = getRecordsSheet_();
  var publicClearsSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_SHEETS.publicClears,
    CORUM_PUBLIC_CLEAR_HEADERS,
  );
  var clearEvidenceSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_SHEETS.clearEvidence,
    CORUM_CLEAR_EVIDENCE_HEADERS,
  );
  var clientVersionsSheet = ensureClientVersionsSheet_();

  playersSheet.setFrozenRows(1);
  clearsSheet.setFrozenRows(1);
  publicClearsSheet.setFrozenRows(1);
  clearEvidenceSheet.setFrozenRows(1);
  clientVersionsSheet.setFrozenRows(1);
  migrateLegacyRecordPercent_(clearsSheet);
  migrateLegacyRecordPercent_(publicClearsSheet);
  ensureMapMinimumRecordColumn_();
  ensureMapAlternateLevelColumn_();
  var createdCsmpTierColumn = ensureMapCsmpTierColumn_();
  var csmpTiersSheet = ensureCsmpTiersSheet_();
  var verifierRecordsSheet = getOrCreateSheet_(
    spreadsheet,
    CORUM_SHEETS.verifierRecords,
    CORUM_VERIFIER_RECORD_HEADERS,
  );
  verifierRecordsSheet.setFrozenRows(1);
  if (createdCsmpTierColumn) seedDefaultCsmpMapAssignments_();
  applyCsmpTierValidation_(csmpTiersSheet);
  var mapsForRecordTitles = readMaps_();
  getRecordSheets_().forEach(function (sheet) {
    migrateLegacyRecordPercent_(sheet);
    migrateLegacyScoreSnapshots_(sheet);
    backfillRecordMapTitles_(sheet, mapsForRecordTitles);
  });
  migrateLegacyScoreSnapshots_(publicClearsSheet);
  readAllClearRecords_().forEach(function (record) {
    if (record.recordId) syncPublicClearRecord_(record);
  });
  syncVerifierRecordSnapshots_(readMaps_());
  var reconciledEvidenceCount = reconcileClearEvidenceLinks_();
  var evidenceFolder = getClearEvidenceFolder_();

  console.log("Corum Integration 시트 준비 완료");
  console.log("맵 시트: " + getMapsSheet_().getName());
  console.log("플레이어 시트: " + playersSheet.getName());
  console.log("클리어 시트: " + clearsSheet.getName());
  console.log("웹 공개용 클리어 시트: " + publicClearsSheet.getName());
  console.log("엔드스크린 증거 시트: " + clearEvidenceSheet.getName());
  console.log("엔드스크린 원본 PNG 폴더: " + evidenceFolder.getName());
  console.log("클라이언트 버전 정책 시트: " + clientVersionsSheet.getName());
  console.log("기존 기록에 다시 연결한 엔드스크린 증거: " + reconciledEvidenceCount + "건");
  console.log("CSMP 티어 시트: " + csmpTiersSheet.getName());
  console.log("Verifier 최초 기록 시트: " + verifierRecordsSheet.getName());
  console.log("Verifier는 CorumPlayers에 임시 가입 상태로 자동 생성됩니다.");
  console.log("같은 Geometry Dash 닉네임의 첫 기록이 제출되면 임시 계정이 실제 GD 계정 ID로 자동 승격됩니다.");
  console.log("점수는 최고 기록이 갱신된 제출 시점의 값으로 다시 확정됩니다.");
}

/**
 * Normal Mode 100% 완료 시 모드가 자동으로 보낸 원본 End Level PNG를 저장한다.
 * 파일은 공개 공유하지 않고 스크립트 소유자의 Drive 폴더에 그대로 보관한다.
 */
function submitClearEvidence_(body) {
  var requestedLevelId = requirePositiveInteger_(body.levelId, "맵 코드");
  var accountId = requirePositiveInteger_(body.gdAccountId, "GD 계정 ID");
  var gdUsername = requireShortText_(body.gdUsername, "Geometry Dash 닉네임", 32);
  var imageBase64 = String(body.imageBase64 || "").trim();

  if (!imageBase64) {
    return jsonError_("IMAGE_REQUIRED", "엔드스크린 PNG가 없습니다.");
  }
  if (String(body.mimeType || "image/png").toLowerCase() !== "image/png") {
    return jsonError_("INVALID_IMAGE_TYPE", "PNG 이미지만 저장할 수 있습니다.");
  }

  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);
  var map = mapsByLevelId[String(requestedLevelId)];
  if (!map) {
    return jsonError_("MAP_NOT_FOUND", "Corum 목록에 없는 맵입니다.");
  }
  var levelId = String(map.levelId);

  var bytes;
  try {
    bytes = Utilities.base64Decode(imageBase64);
  } catch (error) {
    return jsonError_("INVALID_IMAGE", "엔드스크린 PNG를 해석할 수 없습니다.");
  }

  var pngInfo = readPngInfo_(bytes);
  if (!pngInfo) {
    return jsonError_("INVALID_PNG", "올바른 PNG 원본이 아닙니다.");
  }

  var uploadedAt = new Date().toISOString();
  var capturedAt = safeClientTimestamp_(body.clientTimestamp) || uploadedAt;
  var loadedMods = normalizeLoadedMods_(body.loadedMods);
  var evidenceId = Utilities.getUuid();
  var safePlayer = gdUsername.replace(/[^A-Za-z0-9_.-]+/g, "_").slice(0, 32) || "player";
  var fileName = [
    "corum-clear",
    safePlayer,
    String(requestedLevelId),
    evidenceId,
  ].join("-") + ".png";

  var lock = LockService.getScriptLock();
  lock.waitLock(30000);

  try {
    var folder = getClearEvidenceFolder_();
    var blob = Utilities.newBlob(bytes, "image/png", fileName);
    var file = folder.createFile(blob);
    var fileUrl = file.getUrl();
    var evidenceSheet = getClearEvidenceSheet_();

    var evidenceRowObject = {
      "증거 ID": evidenceId,
      "맵 코드": levelId,
      "클리어한 맵 코드": String(requestedLevelId),
      "GD 계정 ID": accountId,
      "플레이어": gdUsername,
      "캡처 시각": capturedAt,
      "업로드 시각": uploadedAt,
      "파일 ID": file.getId(),
      "파일 URL": fileUrl,
      "파일명": fileName,
      "MIME": "image/png",
      "원본 폭": pngInfo.width,
      "원본 높이": pngInfo.height,
      "파일 크기(bytes)": bytes.length,
      "모드 버전": safeText_(body.modVersion, 32),
      "게임 버전": safeText_(body.gameVersion, 32),
      "Geode 버전": safeText_(body.geodeVersion, 32),
      "플랫폼": safeText_(body.platform, 32),
      "연결 레코드 ID": "",
    };
    if (loadedMods.present) {
      evidenceRowObject["사용 모드 목록"] = loadedMods.text;
    }
    appendObjectRow_(evidenceSheet, evidenceRowObject);

    var evidence = {
      id: evidenceId,
      levelId: levelId,
      requestedLevelId: String(requestedLevelId),
      accountId: String(accountId),
      fileId: file.getId(),
      fileUrl: fileUrl,
      fileName: fileName,
      width: pngInfo.width,
      height: pngInfo.height,
      sizeBytes: bytes.length,
      capturedAt: capturedAt,
      uploadedAt: uploadedAt,
      rowNumber: evidenceSheet.getLastRow(),
    };

    // 사용자가 엔드스크린 직후 매우 빠르게 Submit을 눌러 Records가 먼저
    // 만들어진 경우에도 나중에 끝난 이미지 업로드가 그 기록에 연결된다.
    var linkedRecordId = linkEvidenceToExistingRecord_(
      evidence,
      accountId,
      levelId,
      mapsByLevelId,
    );

    return json_({
      ok: true,
      evidence: {
        id: evidenceId,
        levelId: levelId,
        sourceLevelId: String(requestedLevelId),
        width: pngInfo.width,
        height: pngInfo.height,
        sizeBytes: bytes.length,
        uploadedAt: uploadedAt,
        linkedRecordId: linkedRecordId,
      },
    });
  } finally {
    lock.releaseLock();
  }
}

function getClearEvidenceSheet_() {
  return getOrCreateSheet_(
    getSpreadsheet_(),
    CORUM_SHEETS.clearEvidence,
    CORUM_CLEAR_EVIDENCE_HEADERS,
  );
}

function clearEvidenceGeneration_() {
  return String(getClearEvidenceSheet_().getSheetId());
}

function getClearEvidenceFolder_() {
  var properties = PropertiesService.getScriptProperties();
  var propertyName = "CORUM_CLEAR_EVIDENCE_FOLDER_ID";
  var folderId = String(properties.getProperty(propertyName) || "").trim();

  if (folderId) {
    try {
      return DriveApp.getFolderById(folderId);
    } catch (error) {
      console.warn("기존 엔드스크린 폴더를 열 수 없어 새 폴더를 만듭니다.");
    }
  }

  var folder = DriveApp.createFolder("Corum Clear Evidence");
  properties.setProperty(propertyName, folder.getId());
  return folder;
}

function readPngInfo_(bytes) {
  if (!bytes || bytes.length < 24) return null;
  var signature = [137, 80, 78, 71, 13, 10, 26, 10];
  for (var index = 0; index < signature.length; index += 1) {
    if ((bytes[index] & 255) !== signature[index]) return null;
  }
  if (
    (bytes[12] & 255) !== 73 ||
    (bytes[13] & 255) !== 72 ||
    (bytes[14] & 255) !== 68 ||
    (bytes[15] & 255) !== 82
  ) {
    return null;
  }

  var width = pngUInt32_(bytes, 16);
  var height = pngUInt32_(bytes, 20);
  if (!width || !height) return null;
  return { width: width, height: height };
}

function pngUInt32_(bytes, offset) {
  return (
    (bytes[offset] & 255) * 16777216 +
    (bytes[offset + 1] & 255) * 65536 +
    (bytes[offset + 2] & 255) * 256 +
    (bytes[offset + 3] & 255)
  );
}

function readClearEvidenceIndex_(accountId) {
  var sheet = getClearEvidenceSheet_();
  var values = sheet.getDataRange().getValues();
  var index = { byId: {}, latestByLevel: {} };
  if (values.length < 2) return index;

  var header = values[0];
  var evidenceColumn = findHeaderIndex_(header, ["증거 ID"]);
  var levelColumn = findHeaderIndex_(header, ["맵 코드"]);
  var accountColumn = findHeaderIndex_(header, ["GD 계정 ID"]);
  var fileUrlColumn = findHeaderIndex_(header, ["파일 URL"]);
  var uploadedAtColumn = findHeaderIndex_(header, ["업로드 시각"]);
  var targetAccountId = String(accountId);

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    if (String(row[accountColumn]).trim() !== targetAccountId) continue;

    var evidenceId = String(row[evidenceColumn] || "").trim();
    var levelId = String(row[levelColumn] || "").trim();
    if (!evidenceId || !levelId) continue;

    var item = {
      id: evidenceId,
      levelId: levelId,
      accountId: targetAccountId,
      fileUrl: String(row[fileUrlColumn] || "").trim(),
      uploadedAt: String(row[uploadedAtColumn] || "").trim(),
      rowNumber: rowIndex + 1,
    };
    index.byId[evidenceId] = item;

    var previous = index.latestByLevel[levelId];
    var previousTime = previous ? Date.parse(previous.uploadedAt) || 0 : -1;
    var candidateTime = Date.parse(item.uploadedAt) || 0;
    if (!previous || candidateTime >= previousTime) {
      index.latestByLevel[levelId] = item;
    }
  }

  return index;
}

function resolveClearEvidence_(index, evidenceId, levelId) {
  var requestedEvidenceId = String(evidenceId || "").trim();
  var canonicalLevelId = String(levelId || "").trim();

  if (requestedEvidenceId) {
    var exact = index.byId[requestedEvidenceId];
    if (exact && exact.levelId === canonicalLevelId) return exact;
  }
  return index.latestByLevel[canonicalLevelId] || null;
}

function evidenceRecordFields_(evidence) {
  if (!evidence) return {};
  return {
    "증거": evidence.fileUrl,
    "엔드스크린 증거 ID": evidence.id,
    "엔드스크린 파일 URL": evidence.fileUrl,
  };
}

function linkClearEvidenceToRecord_(evidence, recordId) {
  if (!evidence || !evidence.rowNumber || !recordId) return;
  updateObjectRow_(getClearEvidenceSheet_(), evidence.rowNumber, {
    "연결 레코드 ID": recordId,
  });
}

function linkEvidenceToExistingRecord_(evidence, accountId, levelId, mapsByLevelId) {
  var sheet = getRecordsSheet_();
  var values = sheet.getDataRange().getValues();
  if (values.length < 2) return "";

  var header = values[0];
  var recordColumn = findHeaderIndex_(header, ["레코드 ID"]);
  var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
  var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
  var percentColumn = findHeaderIndex_(
    header,
    ["최고 기록(%)", "기록(%)", "Record", "Percent"],
  );
  var bestRowIndex = -1;
  var bestPercent = 0;

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var rowLevelId = String(values[rowIndex][levelColumn] || "").trim();
    var canonicalLevelId = canonicalLevelId_(rowLevelId, mapsByLevelId) || rowLevelId;
    if (
      canonicalLevelId !== String(levelId) ||
      String(values[rowIndex][accountColumn] || "").trim() !== String(accountId)
    ) {
      continue;
    }

    var percent = parseRecordPercent_(values[rowIndex][percentColumn]);
    if (percent >= 100 && (bestRowIndex === -1 || percent >= bestPercent)) {
      bestRowIndex = rowIndex;
      bestPercent = percent;
    }
  }

  if (bestRowIndex === -1) return "";

  var recordId = String(values[bestRowIndex][recordColumn] || "").trim();
  if (!recordId) return "";
  updateObjectRow_(sheet, bestRowIndex + 1, evidenceRecordFields_(evidence));
  linkClearEvidenceToRecord_(evidence, recordId);
  var updatedRow = sheet
    .getRange(bestRowIndex + 1, 1, 1, sheet.getLastColumn())
    .getValues()[0];
  syncPublicClearRecord_(publicClearRecord_(header, updatedRow));
  return recordId;
}

/**
 * ClearEvidence에는 정상 저장됐지만 Records/Public 동기화가 빠진 기존 행을
 * 계정+대표 맵 기준의 최신 증거로 다시 연결한다.
 */
function reconcileClearEvidenceLinks_() {
  var evidenceSheet = getClearEvidenceSheet_();
  var evidenceValues = evidenceSheet.getDataRange().getValues();
  if (evidenceValues.length < 2) return 0;

  var evidenceHeader = evidenceValues[0];
  var evidenceIdColumn = findHeaderIndex_(evidenceHeader, ["증거 ID"]);
  var evidenceLevelColumn = findHeaderIndex_(evidenceHeader, ["맵 코드"]);
  var evidenceAccountColumn = findHeaderIndex_(evidenceHeader, ["GD 계정 ID"]);
  var evidenceUrlColumn = findHeaderIndex_(evidenceHeader, ["파일 URL"]);
  var evidenceUploadedColumn = findHeaderIndex_(evidenceHeader, ["업로드 시각"]);
  var evidenceLinkedRecordColumn = findHeaderIndex_(evidenceHeader, ["연결 레코드 ID"]);
  var latestByScope = {};

  for (var evidenceRowIndex = 1; evidenceRowIndex < evidenceValues.length; evidenceRowIndex += 1) {
    var evidenceRow = evidenceValues[evidenceRowIndex];
    var evidenceId = String(evidenceRow[evidenceIdColumn] || "").trim();
    var levelId = String(evidenceRow[evidenceLevelColumn] || "").trim();
    var accountId = String(evidenceRow[evidenceAccountColumn] || "").trim();
    var fileUrl = String(evidenceRow[evidenceUrlColumn] || "").trim();
    if (!evidenceId || !levelId || !accountId || !fileUrl) continue;

    var evidence = {
      id: evidenceId,
      levelId: levelId,
      accountId: accountId,
      fileUrl: fileUrl,
      uploadedAt: String(evidenceRow[evidenceUploadedColumn] || "").trim(),
      rowNumber: evidenceRowIndex + 1,
    };
    var scope = accountId + "|" + levelId;
    var previous = latestByScope[scope];
    var previousTime = previous ? Date.parse(previous.uploadedAt) || 0 : -1;
    var evidenceTime = Date.parse(evidence.uploadedAt) || 0;
    if (!previous || evidenceTime >= previousTime) latestByScope[scope] = evidence;
  }

  var mapsByLevelId = mapLookupByLevelId_(readMapsWithVerifierSnapshots_());
  var recordsSheet = getRecordsSheet_();
  var recordsValues = recordsSheet.getDataRange().getValues();
  if (recordsValues.length < 2) return 0;

  var recordHeader = recordsValues[0];
  var recordIdColumn = findHeaderIndex_(recordHeader, ["레코드 ID", "Record ID"]);
  var recordLevelColumn = findHeaderIndex_(recordHeader, ["맵 코드", "Level ID"]);
  var recordAccountColumn = findHeaderIndex_(recordHeader, ["GD 계정 ID", "Account ID"]);
  var recordPercentColumn = findHeaderIndex_(
    recordHeader,
    ["최고 기록(%)", "기록(%)", "Record", "Percent"],
  );
  var changedRecords = [];
  var linkedRecordIdsByEvidenceRow = {};

  for (var recordRowIndex = 1; recordRowIndex < recordsValues.length; recordRowIndex += 1) {
    var recordRow = recordsValues[recordRowIndex];
    if (parseRecordPercent_(recordRow[recordPercentColumn]) < 100) continue;

    var rawLevelId = String(recordRow[recordLevelColumn] || "").trim();
    var canonicalLevelId = canonicalLevelId_(rawLevelId, mapsByLevelId) || rawLevelId;
    var recordAccountId = String(recordRow[recordAccountColumn] || "").trim();
    var matchingEvidence = latestByScope[recordAccountId + "|" + canonicalLevelId];
    if (!matchingEvidence) continue;

    applyObjectToRow_(recordHeader, recordRow, evidenceRecordFields_(matchingEvidence));
    var publicRecord = publicClearRecord_(recordHeader, recordRow);
    changedRecords.push(publicRecord);
    if (publicRecord.recordId) {
      linkedRecordIdsByEvidenceRow[matchingEvidence.rowNumber] = publicRecord.recordId;
    }
  }

  if (changedRecords.length === 0) return 0;

  recordsSheet
    .getRange(2, 1, recordsValues.length - 1, recordHeader.length)
    .setValues(recordsValues.slice(1));

  Object.keys(linkedRecordIdsByEvidenceRow).forEach(function (rowNumberText) {
    var rowNumber = Number(rowNumberText);
    evidenceValues[rowNumber - 1][evidenceLinkedRecordColumn] =
      linkedRecordIdsByEvidenceRow[rowNumberText];
  });
  evidenceSheet
    .getRange(2, evidenceLinkedRecordColumn + 1, evidenceValues.length - 1, 1)
    .setValues(evidenceValues.slice(1).map(function (row) {
      return [row[evidenceLinkedRecordColumn]];
    }));

  syncPublicClearRecords_(changedRecords);
  return changedRecords.length;
}

/**
 * 운영자가 등록 기록의 검증 상태와 증거 URL을 수정할 때 사용한다.
 * status는 unverified, verified, rejected 중 하나다.
 */
function setClearStatus(recordId, status, proofUrl) {
  var canonicalRecordId = requireShortText_(recordId, "레코드 ID", 80);
  var canonicalStatus = String(status || "").trim().toLowerCase();
  var canonicalProofUrl = String(proofUrl || "").trim();

  if (["unverified", "verified", "rejected"].indexOf(canonicalStatus) === -1) {
    throw new Error("상태는 unverified, verified, rejected 중 하나여야 합니다.");
  }

  if (canonicalProofUrl && !/^https?:\/\/\S+$/i.test(canonicalProofUrl)) {
    throw new Error("증거 URL은 http:// 또는 https:// 주소여야 합니다.");
  }

  if (canonicalProofUrl.length > 2048) {
    throw new Error("증거 URL이 너무 깁니다.");
  }

  var lock = LockService.getScriptLock();
  lock.waitLock(10000);

  try {
    var updatedRecord = null;

    getRecordSheets_().forEach(function (sheet) {
      var values = sheet.getDataRange().getValues();
      var header = values[0];
      var recordColumn = findHeaderIndex_(header, ["레코드 ID", "Record ID"]);
      var statusColumn = findHeaderIndex_(header, ["상태", "Status"]);
      var proofColumn = findHeaderIndex_(header, ["증거", "Proof URL"]);

      for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
        if (String(values[rowIndex][recordColumn]).trim() !== canonicalRecordId) continue;

        sheet.getRange(rowIndex + 1, statusColumn + 1).setValue(canonicalStatus);
        sheet.getRange(rowIndex + 1, proofColumn + 1).setValue(canonicalProofUrl);
        values[rowIndex][statusColumn] = canonicalStatus;
        values[rowIndex][proofColumn] = canonicalProofUrl;
        updatedRecord = mergeDuplicateClearRecords_(
          updatedRecord,
          publicClearRecord_(header, values[rowIndex]),
        );
      }
    });

    if (updatedRecord) {
      syncPublicClearRecord_(updatedRecord);
      console.log("클리어 상태 변경 완료: " + canonicalRecordId + " → " + canonicalStatus);
      return true;
    }
  } finally {
    lock.releaseLock();
  }

  throw new Error("해당 레코드 ID를 찾을 수 없습니다.");
}

function submitRecord_(body) {
  var versionRejection = clientVersionRejection_(body);
  if (versionRejection) return versionRejection;

  var requestedLevelId = requirePositiveInteger_(body.levelId, "맵 코드");
  var accountId = requirePositiveInteger_(body.gdAccountId, "GD 계정 ID");
  var gdUsername = requireShortText_(body.gdUsername, "Geometry Dash 닉네임", 32);
  var percent = boundedInteger_(body.percent, 1, 100, "최고 기록");
  var attempts = boundedInteger_(body.attempts, 0, 999999999, "시도 횟수");
  var jumps = boundedInteger_(body.jumps, 0, 999999999, "점프");
  var playTimeMs = boundedInteger_(body.playTimeMs, 0, 864000000, "플레이 시간");
  var loadedMods = normalizeLoadedMods_(body.loadedMods);

  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);
  var map = mapsByLevelId[String(requestedLevelId)];
  if (!map) {
    return jsonError_("MAP_NOT_FOUND", "Corum 목록에 없는 맵입니다.");
  }
  var levelId = String(map.levelId);

  if (percent < map.minimumRecord) {
    return jsonError_(
      "BELOW_MINIMUM",
      "최고 기록이 이 맵의 최소 등록 가능 기록 " + map.minimumRecord + "%보다 낮습니다.",
    );
  }

  var lock = LockService.getScriptLock();
  lock.waitLock(10000);

  try {
    var playerRegistration = ensureAutoRegisteredPlayer_(accountId, gdUsername);
    if (!playerRegistration.enabled) {
      return jsonError_("PLAYER_DISABLED", "이 Geometry Dash 계정의 기록 등록이 차단됐습니다.");
    }

    var sheet = getRecordsSheet_();
    migrateLegacyScoreSnapshots_(sheet);
    var clearEvidence = percent >= 100
      ? resolveClearEvidence_(
          readClearEvidenceIndex_(accountId),
          safeText_(body.evidenceId, 80),
          levelId,
        )
      : null;
    var values = sheet.getDataRange().getValues();
    var header = values[0];
    var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
    var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
    var playerColumn = findHeaderIndex_(header, ["플레이어", "Player"]);
    var percentColumn = findHeaderIndex_(header, ["최고 기록(%)", "기록(%)", "Record", "Percent"]);

    var existingRowIndex = -1;
    var existingPercent = 0;

    for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
      var rowLevelId = String(values[rowIndex][levelColumn]).trim();
      var rowAccountId = String(values[rowIndex][accountColumn]).trim();
      var rowCanonicalLevelId =
        canonicalLevelId_(rowLevelId, mapsByLevelId) || rowLevelId;

      if (
        rowCanonicalLevelId === levelId &&
        rowAccountId === String(accountId)
      ) {
        var candidatePercent = parseRecordPercent_(values[rowIndex][percentColumn]);
        if (existingRowIndex === -1 || candidatePercent > existingPercent) {
          existingRowIndex = rowIndex;
          existingPercent = candidatePercent;
        }
      }
    }

    if (existingRowIndex !== -1) {
      var existingRow = values[existingRowIndex];
      applyObjectToRow_(header, existingRow, {
        "맵 코드": levelId,
        "맵 제목": map.title,
        "플레이어": gdUsername,
      });
      if (loadedMods.present) {
        applyObjectToRow_(header, existingRow, {
          "사용 모드 목록": loadedMods.text,
        });
      }
      if (clearEvidence) {
        applyObjectToRow_(header, existingRow, evidenceRecordFields_(clearEvidence));
      }

      if (percent <= existingPercent) {
        sheet
          .getRange(existingRowIndex + 1, 1, 1, sheet.getLastColumn())
          .setValues([existingRow]);
        var unchangedRecord = publicClearRecord_(header, existingRow);
        if (clearEvidence) {
          linkClearEvidenceToRecord_(clearEvidence, unchangedRecord.recordId);
        }
        syncPublicClearRecord_(unchangedRecord);
        return json_({
          ok: true,
          created: false,
          updated: false,
          unchanged: true,
          playerRegistered: playerRegistration.created,
          message: "서버에 등록된 기록과 같거나 더 낮습니다.",
          record: unchangedRecord,
        });
      }

      var updatedAt = new Date().toISOString();
      var updatedScore = getCorumRecordScore_(
        map.rank,
        percent,
        map.minimumRecord,
      );
      var updateObject = {
        "맵 코드": levelId,
        "맵 제목": map.title,
        "GD 계정 ID": accountId,
        "플레이어": gdUsername,
        "최고 기록(%)": percent,
        "클리어 시각": updatedAt,
        "시도 횟수": attempts,
        "점프": jumps,
        "플레이 시간(ms)": playTimeMs,
        "플랫폼": safeText_(body.platform, 32),
        "상태": "unverified",
        "증거": "",
        "모드 버전": safeText_(body.modVersion, 32),
        "게임 버전": safeText_(body.gameVersion, 32),
        "Geode 버전": safeText_(body.geodeVersion, 32),
        "클라이언트 시각": safeClientTimestamp_(body.clientTimestamp),
        "점수 반영 기록(%)": percent,
        "점수 반영 순위": map.rank,
        "점수 반영 최소 기록": map.minimumRecord,
        "확정 점수": updatedScore,
        "점수 공식 버전": CORUM_SCORING_VERSION,
        "점수 확정 시각": updatedAt,
      };
      if (loadedMods.present) {
        updateObject["사용 모드 목록"] = loadedMods.text;
      }
      if (clearEvidence) {
        Object.assign(updateObject, evidenceRecordFields_(clearEvidence));
      }

      updateObjectRow_(sheet, existingRowIndex + 1, updateObject);
      var updatedValues = sheet
        .getRange(existingRowIndex + 1, 1, 1, sheet.getLastColumn())
        .getValues()[0];
      var updatedRecord = publicClearRecord_(header, updatedValues);
      if (clearEvidence) {
        linkClearEvidenceToRecord_(clearEvidence, updatedRecord.recordId);
      }
      syncPublicClearRecord_(updatedRecord);

      return json_({
        ok: true,
        created: false,
        updated: true,
        unchanged: false,
        playerRegistered: playerRegistration.created,
        message: "Corum 최고 기록을 갱신했습니다.",
        record: updatedRecord,
      });
    }

    var recordedAt = new Date().toISOString();
    var recordId = Utilities.getUuid();
    var awardedScore = getCorumRecordScore_(
      map.rank,
      percent,
      map.minimumRecord,
    );
    var rowObject = {
      "레코드 ID": recordId,
      "맵 코드": levelId,
      "맵 제목": map.title,
      "GD 계정 ID": accountId,
      "플레이어": gdUsername,
      "최고 기록(%)": percent,
      "클리어 시각": recordedAt,
      "시도 횟수": attempts,
      "점프": jumps,
      "플레이 시간(ms)": playTimeMs,
      "플랫폼": safeText_(body.platform, 32),
      "상태": "unverified",
      "증거": "",
      "모드 버전": safeText_(body.modVersion, 32),
      "게임 버전": safeText_(body.gameVersion, 32),
      "Geode 버전": safeText_(body.geodeVersion, 32),
      "클라이언트 시각": safeClientTimestamp_(body.clientTimestamp),
      "점수 반영 기록(%)": percent,
      "점수 반영 순위": map.rank,
      "점수 반영 최소 기록": map.minimumRecord,
      "확정 점수": awardedScore,
      "점수 공식 버전": CORUM_SCORING_VERSION,
      "점수 확정 시각": recordedAt,
    };
    if (loadedMods.present) {
      rowObject["사용 모드 목록"] = loadedMods.text;
    }
    if (clearEvidence) {
      Object.assign(rowObject, evidenceRecordFields_(clearEvidence));
    }

    appendObjectRow_(sheet, rowObject);
    var createdRecord = {
      recordId: recordId,
      levelId: String(levelId),
      player: gdUsername,
      percent: percent,
      clearedAt: recordedAt,
      attempts: attempts,
      jumps: jumps,
      playTimeMs: playTimeMs,
      platform: safeText_(body.platform, 32),
      status: "unverified",
      proofUrl: "",
      modVersion: safeText_(body.modVersion, 32),
      scoredPercent: percent,
      scoredRank: map.rank,
      scoredMinimumRecord: map.minimumRecord,
      initialPercent: percent,
      registeredRank: map.rank,
      registeredMinimumRecord: map.minimumRecord,
      score: awardedScore,
      scoringVersion: CORUM_SCORING_VERSION,
      scoreLockedAt: recordedAt,
    };
    if (clearEvidence) {
      createdRecord.proofUrl = clearEvidence.fileUrl;
      createdRecord.evidenceId = clearEvidence.id;
      createdRecord.evidenceUrl = clearEvidence.fileUrl;
      linkClearEvidenceToRecord_(clearEvidence, recordId);
    }
    syncPublicClearRecord_(createdRecord);

    return json_({
      ok: true,
      created: true,
      updated: false,
      unchanged: false,
      playerRegistered: playerRegistration.created,
      message: "Corum 최고 기록을 등록했습니다.",
      record: createdRecord,
      map: map,
    });
  } finally {
    lock.releaseLock();
  }
}

/**
 * 여러 맵의 기록을 하나의 HTTP 요청과 하나의 스크립트 잠금으로 처리한다.
 * 개별 기록의 실패는 results 배열에 남기고 나머지 기록은 계속 처리한다.
 */
function submitBatchRecords_(body) {
  var versionRejection = clientVersionRejection_(body);
  if (versionRejection) return versionRejection;

  var accountId = requirePositiveInteger_(body.gdAccountId, "GD 계정 ID");
  var gdUsername = requireShortText_(body.gdUsername, "Geometry Dash 닉네임", 32);
  var rawRecords = body.records;
  var loadedMods = normalizeLoadedMods_(body.loadedMods);

  if (!Array.isArray(rawRecords) || rawRecords.length < 1) {
    return jsonError_("EMPTY_BATCH", "전송할 기록이 없습니다.");
  }
  if (rawRecords.length > 200) {
    return jsonError_("BATCH_TOO_LARGE", "한 번에 최대 200개 기록만 전송할 수 있습니다.");
  }

  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);

  var results = new Array(rawRecords.length);
  var validatedRecords = [];

  rawRecords.forEach(function (rawRecord, inputIndex) {
    var rawLevelId = rawRecord && rawRecord.levelId;
    var levelIdText = String(rawLevelId == null ? "" : rawLevelId).trim();

    try {
      var requestedLevelId = requirePositiveInteger_(rawLevelId, "맵 코드");
      var percent = boundedInteger_(rawRecord.percent, 1, 100, "최고 기록");
      var attempts = boundedInteger_(rawRecord.attempts, 0, 999999999, "시도 횟수");
      var jumps = boundedInteger_(rawRecord.jumps, 0, 999999999, "점프");
      var playTimeMs = boundedInteger_(
        rawRecord.playTimeMs == null ? 0 : rawRecord.playTimeMs,
        0,
        864000000,
        "플레이 시간",
      );
      var map = mapsByLevelId[String(requestedLevelId)];

      if (!map) {
        results[inputIndex] = batchErrorResult_(
          levelIdText,
          "MAP_NOT_FOUND",
          "Corum 목록에 없는 맵입니다.",
        );
        return;
      }
      if (percent < map.minimumRecord) {
        results[inputIndex] = batchErrorResult_(
          levelIdText,
          "BELOW_MINIMUM",
          "최고 기록이 이 맵의 최소 등록 가능 기록보다 낮습니다.",
        );
        return;
      }

      validatedRecords.push({
        inputIndex: inputIndex,
        levelId: String(map.levelId),
        requestedLevelId: String(requestedLevelId),
        percent: percent,
        attempts: attempts,
        jumps: jumps,
        playTimeMs: playTimeMs,
        evidenceId: safeText_(rawRecord.evidenceId, 80),
        map: map,
      });
    } catch (error) {
      results[inputIndex] = batchErrorResult_(
        levelIdText,
        "INVALID_RECORD",
        error && error.message ? error.message : "기록 값이 올바르지 않습니다.",
      );
    }
  });

  if (validatedRecords.length === 0) {
    return batchResponse_(results, false);
  }

  var lock = LockService.getScriptLock();
  lock.waitLock(15000);

  try {
    var playerRegistration = ensureAutoRegisteredPlayer_(accountId, gdUsername);
    if (!playerRegistration.enabled) {
      return jsonError_("PLAYER_DISABLED", "이 Geometry Dash 계정의 기록 등록이 차단됐습니다.");
    }

    var sheet = getRecordsSheet_();
    migrateLegacyScoreSnapshots_(sheet);

    var width = sheet.getLastColumn();
    var values = sheet.getDataRange().getValues();
    var header = values[0];
    var rows = values.slice(1);
    var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
    var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
    var playerColumn = findHeaderIndex_(header, ["플레이어", "Player"]);
    var percentColumn = findHeaderIndex_(
      header,
      ["최고 기록(%)", "기록(%)", "Record", "Percent"],
    );
    var rowIndexes = {};
    var publicRecords = [];
    var clearEvidenceIndex = readClearEvidenceIndex_(accountId);

    rows.forEach(function (row, rowIndex) {
      var rowLevelId = String(row[levelColumn] || "").trim();
      var canonicalLevelId =
        canonicalLevelId_(rowLevelId, mapsByLevelId) || rowLevelId;
      var key = [
        canonicalLevelId,
        String(row[accountColumn] || "").trim(),
      ].join("|");
      var savedRowIndex = rowIndexes[key];
      if (
        !Object.prototype.hasOwnProperty.call(rowIndexes, key) ||
        parseRecordPercent_(row[percentColumn]) >
          parseRecordPercent_(rows[savedRowIndex][percentColumn])
      ) {
        rowIndexes[key] = rowIndex;
      }
    });

    validatedRecords.forEach(function (entry) {
      var clearEvidence = entry.percent >= 100
        ? resolveClearEvidence_(clearEvidenceIndex, entry.evidenceId, entry.levelId)
        : null;
      var key = String(entry.levelId) + "|" + String(accountId);
      var hasExistingRow = Object.prototype.hasOwnProperty.call(rowIndexes, key);
      var rowIndex = hasExistingRow ? rowIndexes[key] : -1;
      var row = hasExistingRow ? rows[rowIndex] : null;

      if (row) {
        var existingPercent = parseRecordPercent_(row[percentColumn]);
        applyObjectToRow_(header, row, {
          "맵 코드": entry.levelId,
          "맵 제목": entry.map.title,
          "플레이어": gdUsername,
        });
        if (loadedMods.present) {
          applyObjectToRow_(header, row, {
            "사용 모드 목록": loadedMods.text,
          });
        }
        if (clearEvidence) {
          applyObjectToRow_(header, row, evidenceRecordFields_(clearEvidence));
        }

        if (entry.percent <= existingPercent) {
          var unchangedRecord = publicClearRecord_(header, row);
          if (clearEvidence) {
            linkClearEvidenceToRecord_(clearEvidence, unchangedRecord.recordId);
          }
          publicRecords.push(unchangedRecord);
          results[entry.inputIndex] = {
            ok: true,
            levelId: String(entry.levelId),
            created: false,
            updated: false,
            unchanged: true,
            playerRegistered: playerRegistration.created,
            message: "서버에 등록된 기록과 같거나 더 낮습니다.",
            record: unchangedRecord,
          };
          return;
        }

        var updatedAt = new Date().toISOString();
        applyObjectToRow_(header, row, {
          "맵 코드": entry.levelId,
          "맵 제목": entry.map.title,
          "GD 계정 ID": accountId,
          "플레이어": gdUsername,
          "최고 기록(%)": entry.percent,
          "클리어 시각": updatedAt,
          "시도 횟수": entry.attempts,
          "점프": entry.jumps,
          "플레이 시간(ms)": entry.playTimeMs,
          "플랫폼": safeText_(body.platform, 32),
          "상태": "unverified",
          "증거": "",
          "모드 버전": safeText_(body.modVersion, 32),
          "게임 버전": safeText_(body.gameVersion, 32),
          "Geode 버전": safeText_(body.geodeVersion, 32),
          "클라이언트 시각": safeClientTimestamp_(body.clientTimestamp),
          "점수 반영 기록(%)": entry.percent,
          "점수 반영 순위": entry.map.rank,
          "점수 반영 최소 기록": entry.map.minimumRecord,
          "확정 점수": getCorumRecordScore_(
            entry.map.rank,
            entry.percent,
            entry.map.minimumRecord,
          ),
          "점수 공식 버전": CORUM_SCORING_VERSION,
          "점수 확정 시각": updatedAt,
        });

        if (clearEvidence) {
          applyObjectToRow_(header, row, evidenceRecordFields_(clearEvidence));
        }

        var updatedRecord = publicClearRecord_(header, row);
        if (clearEvidence) {
          linkClearEvidenceToRecord_(clearEvidence, updatedRecord.recordId);
        }
        publicRecords.push(updatedRecord);
        results[entry.inputIndex] = {
          ok: true,
          levelId: String(entry.levelId),
          created: false,
          updated: true,
          unchanged: false,
          playerRegistered: playerRegistration.created,
          message: "Corum 최고 기록을 갱신했습니다.",
          record: updatedRecord,
        };
        return;
      }

      var recordedAt = new Date().toISOString();
      var recordId = Utilities.getUuid();
      var rowObject = {
        "레코드 ID": recordId,
        "맵 코드": entry.levelId,
        "맵 제목": entry.map.title,
        "GD 계정 ID": accountId,
        "플레이어": gdUsername,
        "최고 기록(%)": entry.percent,
        "클리어 시각": recordedAt,
        "시도 횟수": entry.attempts,
        "점프": entry.jumps,
        "플레이 시간(ms)": entry.playTimeMs,
        "플랫폼": safeText_(body.platform, 32),
        "상태": "unverified",
        "증거": "",
        "모드 버전": safeText_(body.modVersion, 32),
        "게임 버전": safeText_(body.gameVersion, 32),
        "Geode 버전": safeText_(body.geodeVersion, 32),
        "클라이언트 시각": safeClientTimestamp_(body.clientTimestamp),
        "점수 반영 기록(%)": entry.percent,
        "점수 반영 순위": entry.map.rank,
        "점수 반영 최소 기록": entry.map.minimumRecord,
        "확정 점수": getCorumRecordScore_(
          entry.map.rank,
          entry.percent,
          entry.map.minimumRecord,
        ),
        "점수 공식 버전": CORUM_SCORING_VERSION,
        "점수 확정 시각": recordedAt,
      };
      if (loadedMods.present) {
        rowObject["사용 모드 목록"] = loadedMods.text;
      }
      if (clearEvidence) {
        Object.assign(rowObject, evidenceRecordFields_(clearEvidence));
      }
      var newRow = objectRow_(header, rowObject);
      rows.push(newRow);
      rowIndexes[key] = rows.length - 1;

      var createdRecord = publicClearRecord_(header, newRow);
      if (clearEvidence) {
        linkClearEvidenceToRecord_(clearEvidence, recordId);
      }
      publicRecords.push(createdRecord);
      results[entry.inputIndex] = {
        ok: true,
        levelId: String(entry.levelId),
        created: true,
        updated: false,
        unchanged: false,
        playerRegistered: playerRegistration.created,
        message: "Corum 최고 기록을 등록했습니다.",
        record: createdRecord,
        map: entry.map,
      };
    });

    if (rows.length > 0) {
      sheet.getRange(2, 1, rows.length, width).setValues(rows);
    }
    syncPublicClearRecords_(publicRecords);
    return batchResponse_(results, playerRegistration.created);
  } finally {
    lock.releaseLock();
  }
}

function batchErrorResult_(levelId, code, message) {
  return {
    ok: false,
    levelId: String(levelId || ""),
    error: {
      code: code,
      message: message,
    },
  };
}

function batchResponse_(results, playerRegistered) {
  var succeeded = 0;
  var failed = 0;

  results.forEach(function (result) {
    if (result && result.ok) succeeded += 1;
    else failed += 1;
  });

  return json_({
    ok: true,
    batch: true,
    playerRegistered: Boolean(playerRegistered),
    requested: results.length,
    succeeded: succeeded,
    failed: failed,
    results: results,
  });
}

/*
 * v1 클라이언트에서 호출하던 내부 함수명과의 호환성을 유지한다.
 */
function recordClear_(body) {
  return submitRecord_(body);
}

function getMapResponse_(rawLevelId, rawAccountId) {
  var levelId;
  try {
    levelId = requirePositiveInteger_(rawLevelId, "맵 코드");
  } catch (error) {
    return jsonError_("INVALID_LEVEL_ID", error.message);
  }

  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);
  var map = mapsByLevelId[String(levelId)];
  if (!map) {
    return jsonError_("MAP_NOT_FOUND", "Corum 목록에 없는 맵입니다.");
  }

  var payload = { ok: true, map: map };
  var accountIdText = String(rawAccountId == null ? "" : rawAccountId).trim();
  if (accountIdText) {
    var accountId;
    try {
      accountId = requirePositiveInteger_(accountIdText, "GD 계정 ID");
    } catch (error) {
      return jsonError_("INVALID_ACCOUNT_ID", error.message);
    }

    payload.playerRecord = findPlayerRecord_(
      map.levelId,
      accountId,
      mapsByLevelId,
      maps,
    );
  }

  return json_(payload);
}

function getClearsResponse_(rawLevelId, rawLimit) {
  var levelId;
  try {
    levelId = requirePositiveInteger_(rawLevelId, "맵 코드");
  } catch (error) {
    return jsonError_("INVALID_LEVEL_ID", error.message);
  }

  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);
  var map = mapsByLevelId[String(levelId)];
  var canonicalLevelId = map ? String(map.levelId) : String(levelId);
  var limit = Math.min(Math.max(Number(rawLimit) || 50, 1), 200);
  var bestByPlayer = {};

  readAllClearRecords_(mapsByLevelId)
    .concat(readVerifierRecordSnapshots_(maps, false))
    .forEach(function (record) {
    if (String(record.levelId).trim() !== canonicalLevelId) return;

    var accountId = String(record.accountId || "").trim();
    var player = String(record.player || "").trim();
    var verifierKey = String(map && map.verifier || "").trim().toLowerCase();
    var playerKey = accountId
      ? "account:" + accountId
      : verifierKey && player.toLowerCase() === verifierKey
        ? "verifier:" + verifierKey
        : "player:" + player.toLowerCase();
    var current = bestByPlayer[playerKey];
    var percent = Number(record.percent) || 0;
    var timestamp = Date.parse(record.clearedAt) || 0;

    if (
      shouldReplaceBestRecord_(current, record, percent, timestamp)
    ) {
      bestByPlayer[playerKey] = record;
    }
  });

  var records = Object.keys(bestByPlayer)
    .map(function (playerKey) {
      return bestByPlayer[playerKey];
    })
    .sort(function (left, right) {
      if (Boolean(left.isVerifierRecord) !== Boolean(right.isVerifierRecord)) {
        return left.isVerifierRecord ? -1 : 1;
      }
      if (Number(right.percent) !== Number(left.percent)) {
        return Number(right.percent) - Number(left.percent);
      }
      return (Date.parse(right.clearedAt) || 0) - (Date.parse(left.clearedAt) || 0);
    })
    .slice(0, limit);

  return json_({ ok: true, levelId: canonicalLevelId, records: records });
}

function getCorumBaseScore_(rank) {
  var normalizedRank = Math.floor(Number(rank));
  if (!isFinite(normalizedRank) || normalizedRank < 1) return 0;

  if (normalizedRank === 1) return 350;

  if (normalizedRank <= 5) {
    return 300 * Math.pow(220 / 300, (normalizedRank - 2) / 3);
  }

  if (normalizedRank <= 10) {
    return 200 * Math.pow(140 / 200, (normalizedRank - 6) / 4);
  }

  if (normalizedRank <= 25) {
    return 130 * Math.pow(50 / 130, (normalizedRank - 11) / 14);
  }

  return 45 * Math.pow(0.94, normalizedRank - 26);
}

function getCorumRecordScore_(rank, progress, minimumRecord) {
  var baseScore = getCorumBaseScore_(rank);
  var normalizedProgress = Number(progress);
  var normalizedMinimum = normalizeMinimumRecord_(minimumRecord);

  if (
    !baseScore ||
    !isFinite(normalizedProgress) ||
    normalizedProgress < normalizedMinimum
  ) {
    return 0;
  }

  if (normalizedProgress >= 100) return baseScore;

  return (
    baseScore *
    Math.pow(
      5,
      (normalizedProgress - normalizedMinimum) / (100 - normalizedMinimum),
    ) /
    10
  );
}

function snapshotNumber_(value) {
  if (value == null || String(value).trim() === "") return null;
  var number = Number(value);
  return Number.isFinite(number) ? number : null;
}

function positiveSnapshotNumber_(value) {
  var number = snapshotNumber_(value);
  return number !== null && number > 0 ? number : null;
}

function shouldReplaceBestRecord_(current, candidate, candidatePercent, candidateTimestamp) {
  if (!current) return true;

  var currentPercent = Number(current.percent) || 0;
  if (candidatePercent !== currentPercent) return candidatePercent > currentPercent;

  var currentIsVerifier = Boolean(current.isVerifierRecord);
  var candidateIsVerifier = Boolean(candidate && candidate.isVerifierRecord);
  if (currentIsVerifier !== candidateIsVerifier) return candidateIsVerifier;

  return candidateTimestamp > (Date.parse(current.clearedAt) || current.timestamp || 0);
}

function frozenRecordScore_(record, map) {
  var frozenScore = snapshotNumber_(record.score);
  if (frozenScore !== null && frozenScore >= 0) return frozenScore;

  var rank =
    positiveSnapshotNumber_(record.scoredRank) ||
    positiveSnapshotNumber_(record.registeredRank) ||
    map.rank;
  var progress =
    positiveSnapshotNumber_(record.scoredPercent) ||
    positiveSnapshotNumber_(record.initialPercent) ||
    record.percent;
  var minimumRecord =
    positiveSnapshotNumber_(record.scoredMinimumRecord) ||
    positiveSnapshotNumber_(record.registeredMinimumRecord);
  if (!minimumRecord || minimumRecord < 1 || minimumRecord > 100) {
    minimumRecord = map.minimumRecord;
  }

  return getCorumRecordScore_(rank, progress, minimumRecord);
}

function findPlayerRecord_(levelId, accountId, existingMapLookup, existingMaps) {
  var maps = existingMaps || readMapsWithVerifierSnapshots_();
  var mapsByLevelId = existingMapLookup || mapLookupByLevelId_(maps);
  var map = mapsByLevelId[String(levelId)];
  if (!map) return null;
  var canonicalLevelId = String(map.levelId);

  var bestRecord = null;
  readAllClearRecords_(mapsByLevelId)
    .concat(readVerifierRecordSnapshots_(maps, true))
    .forEach(function (record) {
    if (
      String(record.levelId).trim() !== canonicalLevelId ||
      String(record.accountId).trim() !== String(accountId)
    ) {
      return;
    }

    if (
      shouldReplaceBestRecord_(
        bestRecord,
        record,
        Number(record.percent),
        Date.parse(record.clearedAt) || 0,
      )
    ) {
      bestRecord = record;
    }
  });

  if (!bestRecord) return null;

  var scoredPercent =
    positiveSnapshotNumber_(bestRecord.scoredPercent) ||
    positiveSnapshotNumber_(bestRecord.initialPercent) ||
    bestRecord.percent;
  var scoredRank =
    positiveSnapshotNumber_(bestRecord.scoredRank) ||
    positiveSnapshotNumber_(bestRecord.registeredRank) ||
    map.rank;
  var scoredMinimumRecord = positiveSnapshotNumber_(
    bestRecord.scoredMinimumRecord || bestRecord.registeredMinimumRecord,
  );
  if (
    !scoredMinimumRecord ||
    scoredMinimumRecord < 1 ||
    scoredMinimumRecord > 100
  ) {
    scoredMinimumRecord = map.minimumRecord;
  }

  return {
    recordId: bestRecord.recordId,
    percent: bestRecord.percent,
    scoredPercent: scoredPercent,
    scoredRank: Math.floor(scoredRank),
    scoredMinimumRecord: scoredMinimumRecord,
    // API 2.4 클라이언트와의 하위 호환 필드다.
    initialPercent: scoredPercent,
    registeredRank: Math.floor(scoredRank),
    registeredMinimumRecord: scoredMinimumRecord,
    score: frozenRecordScore_(bestRecord, map),
    scoringVersion: bestRecord.scoringVersion || CORUM_SCORING_VERSION,
    scoreLockedAt: bestRecord.scoreLockedAt || bestRecord.clearedAt,
    status: bestRecord.status,
    isVerifierRecord: Boolean(bestRecord.isVerifierRecord),
  };
}

function getScoresResponse_(rawLimit, rawAccountId) {
  var limit = Math.min(Math.max(Number(rawLimit) || 100, 1), 500);
  var targetAccountId = "";
  if (rawAccountId !== undefined && rawAccountId !== null) {
    var rawAccountText = String(rawAccountId).trim();
    if (rawAccountText) {
      targetAccountId = String(
        requirePositiveInteger_(rawAccountText, "GD 계정 ID"),
      );
    }
  }
  var maps = readMapsWithVerifierSnapshots_();
  var mapsByLevelId = mapLookupByLevelId_(maps);
  var registeredPlayers = readRegisteredPlayers_();

  var records = readAllClearRecords_(mapsByLevelId).concat(
    readVerifierRecordSnapshots_(maps, true),
  );
  if (records.length === 0) {
    return json_({
      ok: true,
      scoringVersion: CORUM_SCORING_VERSION,
      scorePolicy: CORUM_SCORE_POLICY,
      generatedAt: new Date().toISOString(),
      players: [],
    });
  }

  var bestByPlayerAndMap = {};

  records.forEach(function (record) {
    var storedLevelId = String(record.levelId || "").trim();
    var map = mapsByLevelId[storedLevelId];
    if (!map) return;
    var levelId = String(map.levelId);

    var player = String(record.player || "").trim();
    if (!player) return;

    var status = String(record.status || "unverified").trim().toLowerCase();
    if (status === "rejected") return;

    var percent = Number(record.percent);
    if (!isFinite(percent)) return;

    var score = frozenRecordScore_(record, map);
    if (!(score > 0)) return;

    var accountId = String(record.accountId || "").trim();
    var playerRegistration = accountId
      ? registeredPlayers.byAccountId[accountId]
      : registeredPlayers.byName[normalizePlayerKey_(player)];
    if (playerRegistration) {
      accountId = playerRegistration.accountId || accountId;
      player = playerRegistration.player || player;
    }
    if (targetAccountId && accountId !== targetAccountId) return;
    var registrationStatus = String(
      record.registrationStatus ||
      (playerRegistration ? playerRegistration.registrationStatus : "") ||
      (accountId ? "registered" : "temporary"),
    ).trim().toLowerCase();
    var playerKey = accountId
      ? "account:" + accountId
      : "player:" + normalizePlayerKey_(player);
    var clearedAt = String(record.clearedAt || "").trim();
    var timestamp = Date.parse(clearedAt) || 0;
    var recordKey = playerKey + "|" + levelId;
    var current = bestByPlayerAndMap[recordKey];

    if (
      shouldReplaceBestRecord_(current, record, percent, timestamp)
    ) {
      bestByPlayerAndMap[recordKey] = {
        playerKey: playerKey,
        accountId: accountId,
        player: player,
        registrationStatus: registrationStatus,
        levelId: levelId,
        percent: percent,
        clearedAt: clearedAt,
        status: status,
        timestamp: timestamp,
        map: map,
        score: score,
        scoredPercent: record.scoredPercent,
        scoredRank: record.scoredRank,
        scoredMinimumRecord: record.scoredMinimumRecord,
        initialPercent: record.initialPercent,
        registeredRank: record.registeredRank,
        registeredMinimumRecord: record.registeredMinimumRecord,
        scoringVersion: record.scoringVersion,
        scoreLockedAt: record.scoreLockedAt,
        isVerifierRecord: Boolean(record.isVerifierRecord),
      };
    }
  });

  var playersByKey = {};

  Object.keys(bestByPlayerAndMap).forEach(function (recordKey) {
    var record = bestByPlayerAndMap[recordKey];
    var score = record.score;
    if (!(score > 0)) return;

    var scoredRank =
      positiveSnapshotNumber_(record.scoredRank) ||
      positiveSnapshotNumber_(record.registeredRank);
    if (!scoredRank) scoredRank = record.map.rank;
    scoredRank = Math.floor(scoredRank);

    var scoredMinimumRecord = positiveSnapshotNumber_(
      record.scoredMinimumRecord || record.registeredMinimumRecord,
    );
    if (
      !scoredMinimumRecord ||
      scoredMinimumRecord < 1 ||
      scoredMinimumRecord > 100
    ) {
      scoredMinimumRecord = record.map.minimumRecord;
    }
    var scoredPercent =
      positiveSnapshotNumber_(record.scoredPercent) ||
      positiveSnapshotNumber_(record.initialPercent) ||
      record.percent;

    var player = playersByKey[record.playerKey];
    if (!player) {
      player = {
        accountId: record.accountId,
        player: record.player,
        registrationStatus: record.registrationStatus,
        score: 0,
        recordCount: 0,
        completions: 0,
        latestTimestamp: 0,
        bestRecord: null,
        records: [],
      };
      playersByKey[record.playerKey] = player;
    }

    var scoredRecord = {
      levelId: record.levelId,
      title: record.map.title,
      rank: scoredRank,
      currentRank: record.map.rank,
      tier: tierForRank_(scoredRank),
      currentTier: record.map.tier,
      rating: record.map.rating,
      percent: record.percent,
      scoredPercent: scoredPercent,
      scoredRank: scoredRank,
      scoredMinimumRecord: scoredMinimumRecord,
      // API 2.4 클라이언트와의 하위 호환 필드다.
      initialPercent: scoredPercent,
      minimumRecord: scoredMinimumRecord,
      score: score,
      scoringVersion: record.scoringVersion || CORUM_SCORING_VERSION,
      scoreLockedAt: record.scoreLockedAt || record.clearedAt,
      clearedAt: record.clearedAt,
      status: record.status,
      isVerifierRecord: Boolean(record.isVerifierRecord),
    };

    player.score += score;
    player.recordCount += 1;
    player.records.push(scoredRecord);
    if (record.percent >= 100) player.completions += 1;

    if (record.timestamp >= player.latestTimestamp) {
      if (record.accountId) player.accountId = record.accountId;
      player.player = record.player;
      player.registrationStatus = record.registrationStatus;
      player.latestTimestamp = record.timestamp;
    }

    if (!player.bestRecord || score > player.bestRecord.score) {
      player.bestRecord = scoredRecord;
    }
  });

  var players = Object.keys(playersByKey)
    .map(function (playerKey) {
      return playersByKey[playerKey];
    })
    .sort(function (left, right) {
      if (right.score !== left.score) return right.score - left.score;
      if (right.completions !== left.completions) {
        return right.completions - left.completions;
      }
      return left.player.toLowerCase() < right.player.toLowerCase() ? -1 : 1;
    });

  var previousScore = null;
  var previousRank = 0;

  players.forEach(function (player, index) {
    var rawScore = player.score;
    var rank =
      previousScore !== null && Math.abs(rawScore - previousScore) < 0.000000001
        ? previousRank
        : index + 1;

    player.rank = rank;
    player.score = Math.round(rawScore * 1000000) / 1000000;
    player.records.sort(function (left, right) {
      if (right.score !== left.score) return right.score - left.score;
      if (left.rank !== right.rank) return left.rank - right.rank;
      return right.percent - left.percent;
    });
    player.records.forEach(function (record) {
      record.score = Math.round(record.score * 1000000) / 1000000;
    });
    player.bestRecord = player.records.length > 0 ? player.records[0] : null;

    delete player.latestTimestamp;
    previousScore = rawScore;
    previousRank = rank;
  });

  return json_({
    ok: true,
    scoringVersion: CORUM_SCORING_VERSION,
    scorePolicy: CORUM_SCORE_POLICY,
    generatedAt: new Date().toISOString(),
    players: players.slice(0, limit),
  });
}

function readMaps_() {
  var sheet = getMapsSheet_();
  var values = sheet.getDataRange().getDisplayValues();
  if (values.length < 2) return [];

  var header = values[0];
  var rankColumn = findHeaderIndex_(header, ["순위", "Rank"]);
  var titleColumn = findHeaderIndex_(header, ["맵 제목", "맵제목", "제목", "Title"]);
  var ratingColumn = findHeaderIndex_(header, ["Rating", "rating", "레이팅"]);
  var lengthColumn = findHeaderIndex_(header, ["맵 길이", "맵길이", "길이", "Length"]);
  var levelColumn = findHeaderIndex_(header, ["맵 코드", "맵코드", "Level ID", "levelId", "ID"]);
  var alternateLevelColumn = findOptionalHeaderIndex_(header, [
    "대체 맵 코드",
    "대체맵코드",
    "Alternate Level ID",
    "Alternative Level ID",
    "Alt Level ID",
    "alternateLevelId",
  ]);
  var creatorColumn = findHeaderIndex_(header, ["제작자", "Creator"]);
  var verifierColumn = findHeaderIndex_(header, ["Verifier", "검증자"]);
  var minimumRecordColumn = findOptionalHeaderIndex_(header, [
    "최소 등록 가능 기록",
    "최소 등록 기록",
    "Minimum Record",
    "Min Record",
  ]);
  var csmpTierColumn = findOptionalHeaderIndex_(header, [
    "CSMP 티어 배정",
    "CSMP 티어",
    "CSMP Tier Assignment",
    "CSMP Tier",
    "csmpTier",
  ]);
  var maps = [];

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var levelId = String(values[rowIndex][levelColumn] || "").trim();
    var title = String(values[rowIndex][titleColumn] || "").trim();
    if (!/^\d+$/.test(levelId) || !title) continue;
    var alternateLevelId = normalizeAlternateLevelId_(
      alternateLevelColumn === -1 ? "" : values[rowIndex][alternateLevelColumn],
      levelId,
    );

    var rank = Number(String(values[rowIndex][rankColumn] || "").replace(/[^0-9.-]/g, "")) || 0;
    var rating = normalizeRating_(values[rowIndex][ratingColumn]);

    maps.push({
      rank: rank,
      tier: tierForRank_(rank),
      title: title,
      rating: rating,
      length: String(values[rowIndex][lengthColumn] || "").trim(),
      levelId: levelId,
      alternateLevelId: alternateLevelId,
      creator: String(values[rowIndex][creatorColumn] || "").trim(),
      verifier: String(values[rowIndex][verifierColumn] || "").trim(),
      minimumRecord: normalizeMinimumRecord_(
        minimumRecordColumn === -1 ? "" : values[rowIndex][minimumRecordColumn],
      ),
      csmpTier: String(
        csmpTierColumn === -1 ? "" : values[rowIndex][csmpTierColumn] || "",
      ).trim(),
    });
  }

  maps.sort(function (left, right) {
    return left.rank - right.rank;
  });

  return maps;
}

function readMapsWithVerifierSnapshots_() {
  var maps = readMaps_();
  syncVerifierRecordSnapshots_(maps);
  return maps;
}

function syncVerifierRecordSnapshots_(maps) {
  var sourceMaps = Array.isArray(maps) ? maps : readMaps_();
  var lock = LockService.getScriptLock();
  lock.waitLock(10000);

  try {
    var sheet = getOrCreateSheet_(
      getSpreadsheet_(),
      CORUM_SHEETS.verifierRecords,
      CORUM_VERIFIER_RECORD_HEADERS,
    );
    sheet.setFrozenRows(1);

    var width = sheet.getLastColumn();
    var values = sheet.getDataRange().getValues();
    var header = values[0];
    var rows = values.slice(1);
    var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
    var rowIndexes = {};
    var changed = false;
    var now = new Date().toISOString();
    ensureTemporaryVerifierPlayers_(sourceMaps);
    var registeredPlayers = readRegisteredPlayers_();

    rows.forEach(function (row, rowIndex) {
      var levelId = String(row[levelColumn] || "").trim();
      if (levelId && !Object.prototype.hasOwnProperty.call(rowIndexes, levelId)) {
        rowIndexes[levelId] = rowIndex;
      }
    });

    function setValue(row, columnName, value, onlyWhenBlank) {
      var column = findHeaderIndex_(header, [columnName]);
      var current = row[column];
      if (onlyWhenBlank && String(current == null ? "" : current).trim() !== "") {
        return;
      }
      if (String(current == null ? "" : current) === String(value == null ? "" : value)) {
        return;
      }
      row[column] = value;
      changed = true;
    }

    sourceMaps.forEach(function (map) {
      var levelId = String(map.levelId || "").trim();
      var verifier = String(map.verifier || "").trim();
      var rank = Math.floor(Number(map.rank));
      if (!levelId || !verifier || !Number.isFinite(rank) || rank < 1) return;
      var registeredPlayer = registeredPlayers.byName[normalizePlayerKey_(verifier)];

      var hasRow = Object.prototype.hasOwnProperty.call(rowIndexes, levelId);
      var row = hasRow
        ? rows[rowIndexes[levelId]]
        : Array.from({ length: width }, function () { return ""; });

      setValue(row, "Verifier 레코드 ID", "verifier:" + levelId, false);
      setValue(row, "맵 코드", levelId, false);
      setValue(row, "맵 제목", map.title, false);
      setValue(row, "Verifier", verifier, false);
      setValue(
        row,
        "Verifier GD 계정 ID",
        registeredPlayer ? registeredPlayer.accountId : "",
        false,
      );
      setValue(row, "Verifier 기록(%)", 100, true);
      setValue(row, "Verifier 등록 당시 순위", rank, true);
      setValue(row, "Verifier 등록 당시 최소 기록", map.minimumRecord, true);
      setValue(
        row,
        "Verifier 확정 점수",
        getCorumRecordScore_(rank, 100, map.minimumRecord),
        true,
      );
      setValue(row, "Verifier 점수 공식 버전", CORUM_SCORING_VERSION, true);
      setValue(row, "Verifier 등록 시각", now, true);

      if (!hasRow) {
        rows.push(row);
        rowIndexes[levelId] = rows.length - 1;
        changed = true;
      }
    });

    if (changed && rows.length > 0) {
      sheet.getRange(2, 1, rows.length, width).setValues(rows);
    }

    return changed;
  } finally {
    lock.releaseLock();
  }
}

/**
 * 맵 시트에 있는 Verifier를 CorumPlayers의 임시 가입 계정으로 준비한다.
 * 실제 계정 ID가 이미 있으면 registered, 없으면 temporary 상태를 유지한다.
 * 호출자는 스크립트 잠금을 보유해야 한다.
 */
function ensureTemporaryVerifierPlayers_(maps) {
  var sheet = getOrCreateSheet_(
    getSpreadsheet_(),
    CORUM_SHEETS.players,
    CORUM_PLAYER_HEADERS,
  );
  var width = sheet.getLastColumn();
  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var rows = values.slice(1);
  var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
  var playerColumn = findHeaderIndex_(header, ["플레이어", "Player"]);
  var statusColumn = findHeaderIndex_(header, ["가입 상태", "Registration Status"]);
  var activeColumn = findHeaderIndex_(header, ["활성", "Active"]);
  var createdColumn = findHeaderIndex_(header, ["생성 시각", "Created At"]);
  var lastActiveColumn = findHeaderIndex_(header, [
    "최근 활동 시각",
    "Last Active At",
  ]);
  var rowsByName = {};
  var changed = false;
  var now = new Date().toISOString();

  rows.forEach(function (row, rowIndex) {
    while (row.length < width) row.push("");
    var player = String(row[playerColumn] || "").trim();
    if (!player) return;

    var accountId = normalizeOptionalAccountId_(row[accountColumn]);
    var expectedStatus = accountId ? "registered" : "temporary";
    if (String(row[statusColumn] || "").trim().toLowerCase() !== expectedStatus) {
      row[statusColumn] = expectedStatus;
      changed = true;
    }

    var playerKey = normalizePlayerKey_(player);
    var currentIndex = rowsByName[playerKey];
    if (
      currentIndex == null ||
      (
        accountId &&
        !normalizeOptionalAccountId_(rows[currentIndex][accountColumn])
      )
    ) {
      rowsByName[playerKey] = rowIndex;
    }
  });

  (Array.isArray(maps) ? maps : []).forEach(function (map) {
    var verifier = String(map && map.verifier || "").trim();
    if (!verifier) return;

    var playerKey = normalizePlayerKey_(verifier);
    if (Object.prototype.hasOwnProperty.call(rowsByName, playerKey)) return;

    var row = Array.from({ length: width }, function () { return ""; });
    row[playerColumn] = verifier;
    row[statusColumn] = "temporary";
    row[activeColumn] = true;
    row[createdColumn] = now;
    row[lastActiveColumn] = now;
    rows.push(row);
    rowsByName[playerKey] = rows.length - 1;
    changed = true;
  });

  if (changed && rows.length > 0) {
    sheet.getRange(2, 1, rows.length, width).setValues(rows);
  }

  return changed;
}

function readRegisteredPlayers_() {
  var sheet = getSpreadsheet_().getSheetByName(CORUM_SHEETS.players);
  if (!sheet || sheet.getLastRow() < 2) {
    return { byAccountId: {}, byName: {} };
  }

  var values = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
  var playerColumn = findHeaderIndex_(header, ["플레이어", "Player"]);
  var statusColumn = findOptionalHeaderIndex_(header, [
    "가입 상태",
    "Registration Status",
  ]);
  var activeColumn = findOptionalHeaderIndex_(header, ["활성", "Active"]);
  var createdColumn = findOptionalHeaderIndex_(header, ["생성 시각", "Created At"]);
  var lastActiveColumn = findOptionalHeaderIndex_(header, [
    "최근 활동 시각",
    "Last Active At",
  ]);
  var players = { byAccountId: {}, byName: {} };

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var accountId = normalizeOptionalAccountId_(values[rowIndex][accountColumn]);
    var player = String(values[rowIndex][playerColumn] || "").trim();
    var active = activeColumn === -1
      ? ""
      : String(values[rowIndex][activeColumn] || "").trim().toLowerCase();
    if (
      !player ||
      ["false", "0", "no", "blocked"].indexOf(active) !== -1
    ) {
      continue;
    }

    var lastActiveAt = lastActiveColumn === -1
      ? ""
      : String(values[rowIndex][lastActiveColumn] || "").trim();
    var createdAt = createdColumn === -1
      ? ""
      : String(values[rowIndex][createdColumn] || "").trim();
    var playerEntry = {
      accountId: accountId,
      player: player,
      registrationStatus: accountId
        ? "registered"
        : (
          statusColumn === -1
            ? "temporary"
            : String(values[rowIndex][statusColumn] || "temporary").trim().toLowerCase()
        ) || "temporary",
      timestamp: Date.parse(lastActiveAt) || Date.parse(createdAt) || 0,
      rowIndex: rowIndex,
    };
    if (accountId) players.byAccountId[accountId] = playerEntry;

    var playerKey = normalizePlayerKey_(player);
    var current = players.byName[playerKey];
    var sameRegistrationKind = Boolean(playerEntry.accountId) === Boolean(current && current.accountId);
    if (
      !current ||
      (playerEntry.accountId && !current.accountId) ||
      (sameRegistrationKind && playerEntry.timestamp > current.timestamp) ||
      (
        sameRegistrationKind &&
        playerEntry.timestamp === current.timestamp &&
        playerEntry.rowIndex > current.rowIndex
      )
    ) {
      players.byName[playerKey] = playerEntry;
    }
  }

  return players;
}

function readVerifierRecordSnapshots_(maps, registeredOnly) {
  var sourceMaps = Array.isArray(maps) ? maps : readMaps_();
  var mapsByLevelId = mapLookupByLevelId_(sourceMaps);
  var sheet = getSpreadsheet_().getSheetByName(CORUM_SHEETS.verifierRecords);
  if (!sheet || sheet.getLastRow() < 2) return [];

  var registeredPlayers = readRegisteredPlayers_();
  var values = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var recordColumn = findHeaderIndex_(header, ["Verifier 레코드 ID", "Record ID"]);
  var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
  var percentColumn = findHeaderIndex_(header, ["Verifier 기록(%)", "Percent"]);
  var rankColumn = findHeaderIndex_(header, [
    "Verifier 등록 당시 순위",
    "Verifier Registered Rank",
  ]);
  var minimumColumn = findHeaderIndex_(header, [
    "Verifier 등록 당시 최소 기록",
    "Verifier Registered Minimum",
  ]);
  var scoreColumn = findHeaderIndex_(header, [
    "Verifier 확정 점수",
    "Verifier Frozen Score",
  ]);
  var scoringVersionColumn = findHeaderIndex_(header, [
    "Verifier 점수 공식 버전",
    "Verifier Scoring Version",
  ]);
  var registeredAtColumn = findHeaderIndex_(header, [
    "Verifier 등록 시각",
    "Verifier Registered At",
  ]);
  var records = [];
  var seenLevelIds = {};

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var levelId = String(row[levelColumn] || "").trim();
    var map = mapsByLevelId[levelId];
    if (!map || seenLevelIds[levelId] || !String(map.verifier || "").trim()) continue;

    var registeredPlayer = registeredPlayers.byName[
      normalizePlayerKey_(map.verifier)
    ];
    var accountId = registeredPlayer ? registeredPlayer.accountId : "";
    if (registeredOnly && !registeredPlayer) continue;

    var percent = positiveSnapshotNumber_(row[percentColumn]) || 100;
    var scoredRank = positiveSnapshotNumber_(row[rankColumn]);
    var scoredMinimumRecord = positiveSnapshotNumber_(row[minimumColumn]);
    if (!scoredRank) continue;
    if (!scoredMinimumRecord || scoredMinimumRecord > 100) {
      scoredMinimumRecord = map.minimumRecord;
    }

    var score = snapshotNumber_(row[scoreColumn]);
    if (score === null || score < 0) {
      score = getCorumRecordScore_(scoredRank, percent, scoredMinimumRecord);
    }
    var registeredAt = String(row[registeredAtColumn] || "").trim();

    records.push({
      recordId: String(row[recordColumn] || "").trim() || "verifier:" + levelId,
      levelId: levelId,
      accountId: accountId,
      player: registeredOnly ? registeredPlayer.player : String(map.verifier).trim(),
      percent: percent,
      clearedAt: registeredAt,
      attempts: 0,
      jumps: 0,
      playTimeMs: 0,
      platform: "Verifier",
      status: "verified",
      proofUrl: "",
      modVersion: "server-verifier",
      scoredPercent: percent,
      scoredRank: scoredRank,
      scoredMinimumRecord: scoredMinimumRecord,
      initialPercent: percent,
      registeredRank: scoredRank,
      registeredMinimumRecord: scoredMinimumRecord,
      score: score,
      scoringVersion:
        String(row[scoringVersionColumn] || "").trim() || CORUM_SCORING_VERSION,
      scoreLockedAt: registeredAt,
      isVerifierRecord: true,
      registrationStatus: registeredPlayer
        ? registeredPlayer.registrationStatus
        : "temporary",
    });
    seenLevelIds[levelId] = true;
  }

  return records;
}

function getCsmpResponse_() {
  var warnings = [];
  var tiers = readCsmpTiers_(warnings);
  var tiersByName = {};

  tiers.forEach(function (tier) {
    tiersByName[normalizeCsmpTierName_(tier.name)] = tier;
  });

  readMapsWithVerifierSnapshots_().forEach(function (map) {
    var assignedName = String(map.csmpTier || "").trim();
    if (!assignedName) return;

    var tier = tiersByName[normalizeCsmpTierName_(assignedName)];
    if (!tier) {
      warnings.push(
        "'" + map.title + "'의 CSMP 티어 '" + assignedName + "'을(를) CSMP Tiers에서 찾을 수 없습니다.",
      );
      return;
    }

    tier.maps.push({
      levelId: String(map.levelId),
      alternateLevelId: String(map.alternateLevelId || ""),
      title: map.title,
      rank: map.rank,
    });
  });

  tiers.forEach(function (tier) {
    tier.maps.sort(function (left, right) {
      if (left.rank !== right.rank) return left.rank - right.rank;
      return left.title.toLowerCase() < right.title.toLowerCase() ? -1 : 1;
    });

    tier.required = tier.requiresAll ? tier.maps.length : tier.requiredCount;
    if (!tier.requiresAll && tier.requiredCount > tier.maps.length) {
      warnings.push(
        "CSMP " + tier.name + "의 최소 클리어 수(" + tier.requiredCount + ")가 배정된 맵 수(" + tier.maps.length + ")보다 큽니다.",
      );
    }
    delete tier.requiredCount;
  });

  return json_({
    ok: true,
    generatedAt: new Date().toISOString(),
    tiers: tiers,
    warnings: warnings,
  });
}

function readCsmpTiers_(warnings) {
  var sheet = getSpreadsheet_().getSheetByName(CORUM_SHEETS.csmpTiers);
  if (!sheet || sheet.getLastRow() < 2) {
    warnings.push("CSMP Tiers 시트에 사용 가능한 티어가 없습니다.");
    return [];
  }

  var values = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var orderColumn = findHeaderIndex_(header, ["진행 순서", "순서", "Order"]);
  var nameColumn = findHeaderIndex_(header, ["티어명", "티어 이름", "Tier", "Tier Name"]);
  var iconColumn = findHeaderIndex_(header, ["아이콘 파일명", "아이콘", "Icon", "Icon File"]);
  var colorColumn = findHeaderIndex_(header, ["대표 색상", "색상", "Color"]);
  var requiredColumn = findHeaderIndex_(header, [
    "최소 클리어 맵 수",
    "최소 클리어 수",
    "Required",
    "Minimum Clears",
  ]);
  var rows = [];

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var name = String(values[rowIndex][nameColumn] || "").trim();
    if (!name) continue;

    var parsedOrder = Number(String(values[rowIndex][orderColumn] || "").trim());
    var order = Number.isFinite(parsedOrder) && parsedOrder > 0
      ? Math.floor(parsedOrder)
      : rowIndex;
    if (!(Number.isFinite(parsedOrder) && parsedOrder > 0)) {
      warnings.push("CSMP Tiers " + (rowIndex + 1) + "행의 진행 순서가 올바르지 않아 행 순서를 사용했습니다.");
    }

    rows.push({
      rowIndex: rowIndex,
      order: order,
      name: name.slice(0, 80),
      iconFile: normalizeCsmpIconFile_(values[rowIndex][iconColumn], name, warnings),
      color: normalizeCsmpColor_(values[rowIndex][colorColumn], name, warnings),
      requirement: parseCsmpRequirement_(values[rowIndex][requiredColumn], name, warnings),
    });
  }

  rows.sort(function (left, right) {
    return left.order === right.order ? left.rowIndex - right.rowIndex : left.order - right.order;
  });

  var names = {};
  var keys = {};
  var tiers = [];

  rows.forEach(function (row) {
    var normalizedName = normalizeCsmpTierName_(row.name);
    if (names[normalizedName]) {
      warnings.push("중복된 CSMP 티어명 '" + row.name + "' 행은 무시했습니다.");
      return;
    }
    names[normalizedName] = true;

    var baseKey = slugifyCsmpTierKey_(row.name) || "tier-" + row.order;
    var key = baseKey;
    var suffix = 2;
    while (keys[key]) {
      key = baseKey + "-" + suffix;
      suffix += 1;
    }
    keys[key] = true;

    tiers.push({
      order: row.order,
      key: key,
      name: row.name,
      iconFile: row.iconFile,
      color: row.color,
      requiresAll: row.requirement.requiresAll,
      requiredCount: row.requirement.requiredCount,
      maps: [],
    });
  });

  return tiers;
}

function findMapByLevelId_(levelId) {
  return mapLookupByLevelId_(readMaps_())[String(levelId).trim()] || null;
}

/**
 * 대표 맵 코드를 먼저 등록하고 대체 코드를 두 번째로 등록한다.
 * 잘못 설정된 대체 코드가 다른 맵의 대표 코드와 겹치더라도 대표 코드가 우선한다.
 */
function mapLookupByLevelId_(maps) {
  var lookup = {};

  maps.forEach(function (map) {
    lookup[String(map.levelId)] = map;
  });

  maps.forEach(function (map) {
    var alternateLevelId = String(map.alternateLevelId || "").trim();
    if (
      alternateLevelId &&
      !Object.prototype.hasOwnProperty.call(lookup, alternateLevelId)
    ) {
      lookup[alternateLevelId] = map;
    }
  });

  return lookup;
}

function canonicalLevelId_(levelId, mapsByLevelId) {
  var map = mapsByLevelId[String(levelId == null ? "" : levelId).trim()];
  return map ? String(map.levelId) : "";
}

/**
 * 첫 기록 제출 시 플레이어를 자동 등록한다.
 * CorumPlayers에서 활성을 명시적으로 FALSE로 바꾼 계정만 차단한다.
 * 호출자는 스크립트 잠금을 보유해야 한다.
 */
function ensureAutoRegisteredPlayer_(accountId, gdUsername) {
  var sheet = getOrCreateSheet_(getSpreadsheet_(), CORUM_SHEETS.players, CORUM_PLAYER_HEADERS);
  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var width = sheet.getLastColumn();
  var accountColumn = findHeaderIndex_(header, ["GD 계정 ID", "Account ID"]);
  var playerColumn = findHeaderIndex_(header, ["플레이어", "Player"]);
  var statusColumn = findHeaderIndex_(header, ["가입 상태", "Registration Status"]);
  var activeColumn = findHeaderIndex_(header, ["활성", "Active"]);
  var createdColumn = findHeaderIndex_(header, ["생성 시각", "Created At"]);
  var lastActiveColumn = findHeaderIndex_(header, ["최근 활동 시각", "Last Active At"]);
  var now = new Date().toISOString();

  function isBlocked(row) {
    var activeValue = row[activeColumn];
    var active = String(activeValue == null ? "" : activeValue).trim().toLowerCase();
    return active === "false" || active === "0" || active === "no" || active === "blocked";
  }

  function promoteRow(rowIndex, promoted) {
    var row = values[rowIndex];
    while (row.length < width) row.push("");
    row[accountColumn] = accountId;
    row[playerColumn] = gdUsername;
    row[statusColumn] = "registered";
    row[activeColumn] = true;
    if (!String(row[createdColumn] || "").trim()) row[createdColumn] = now;
    row[lastActiveColumn] = now;
    sheet.getRange(rowIndex + 1, 1, 1, width).setValues([row]);
    return {
      enabled: true,
      created: Boolean(promoted),
      promoted: Boolean(promoted),
    };
  }

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    if (String(values[rowIndex][accountColumn] || "").trim() !== String(accountId)) continue;

    if (isBlocked(values[rowIndex])) {
      return {
        enabled: false,
        created: false,
      };
    }

    return promoteRow(rowIndex, false);
  }

  var playerKey = normalizePlayerKey_(gdUsername);
  for (var temporaryRowIndex = 1; temporaryRowIndex < values.length; temporaryRowIndex += 1) {
    var temporaryAccountId = normalizeOptionalAccountId_(
      values[temporaryRowIndex][accountColumn],
    );
    var temporaryPlayerKey = normalizePlayerKey_(
      values[temporaryRowIndex][playerColumn],
    );
    if (temporaryAccountId || temporaryPlayerKey !== playerKey) continue;

    if (isBlocked(values[temporaryRowIndex])) {
      return {
        enabled: false,
        created: false,
      };
    }

    return promoteRow(temporaryRowIndex, true);
  }

  appendObjectRow_(sheet, {
    "GD 계정 ID": accountId,
    "플레이어": gdUsername,
    "가입 상태": "registered",
    "활성": true,
    "생성 시각": now,
    "최근 활동 시각": now,
  });
  return {
    enabled: true,
    created: true,
    promoted: false,
  };
}

function publicClearRecord_(header, row) {
  function value(aliases) {
    return String(row[findHeaderIndex_(header, aliases)] || "").trim();
  }

  function optionalValue(aliases) {
    var columnIndex = findOptionalHeaderIndex_(header, aliases);
    return columnIndex === -1 ? "" : String(row[columnIndex] || "").trim();
  }

  function optionalNumber(aliases) {
    var text = optionalValue(aliases);
    if (!text) return null;
    var number = Number(String(text).replace(/%$/, "").trim());
    return Number.isFinite(number) ? number : null;
  }

  var scoredPercent = optionalNumber([
    "점수 반영 기록(%)",
    "최초 등록 기록(%)",
    "최초 등록 기록",
    "Initial Record",
    "Scored Record",
  ]);
  var scoredRank = optionalNumber([
    "점수 반영 순위",
    "최초 등록 순위",
    "등록 당시 순위",
    "Registered Rank",
    "Scored Rank",
  ]);
  var scoredMinimumRecord = optionalNumber([
    "점수 반영 최소 기록",
    "최초 등록 최소 기록",
    "등록 당시 최소 등록 기록",
    "Registered Minimum",
    "Scored Minimum",
  ]);

  return {
    recordId: value(["레코드 ID", "Record ID"]),
    levelId: value(["맵 코드", "Level ID"]),
    accountId: optionalValue(["GD 계정 ID", "Account ID"]),
    player: value(["플레이어", "Player"]),
    percent: Number(value(["최고 기록(%)", "기록(%)", "Record", "Percent"])) || 0,
    clearedAt: value(["클리어 시각", "Cleared At"]),
    attempts: Number(value(["시도 횟수", "Attempts"])) || 0,
    jumps: Number(value(["점프", "Jumps"])) || 0,
    playTimeMs: Number(value(["플레이 시간(ms)", "Play Time (ms)"])) || 0,
    platform: value(["플랫폼", "Platform"]),
    status: value(["상태", "Status"]) || "unverified",
    proofUrl: value(["증거", "Proof URL"]),
    evidenceId: optionalValue(["엔드스크린 증거 ID", "End Screen Evidence ID"]),
    evidenceUrl: optionalValue(["엔드스크린 파일 URL", "End Screen File URL"]),
    modVersion: value(["모드 버전", "Mod Version"]),
    scoredPercent: scoredPercent,
    scoredRank: scoredRank,
    scoredMinimumRecord: scoredMinimumRecord,
    // API 2.4 클라이언트와의 하위 호환 필드다.
    initialPercent: scoredPercent,
    registeredRank: scoredRank,
    registeredMinimumRecord: scoredMinimumRecord,
    score: optionalNumber([
      "확정 점수",
      "최초 등록 점수",
      "Frozen Score",
      "Awarded Score",
    ]),
    scoringVersion: optionalValue(["점수 공식 버전", "Scoring Version"]),
    scoreLockedAt: optionalValue(["점수 확정 시각", "Score Locked At"]),
  };
}

function syncPublicClearRecord_(record) {
  var sheet = getOrCreateSheet_(
    getSpreadsheet_(),
    CORUM_SHEETS.publicClears,
    CORUM_PUBLIC_CLEAR_HEADERS,
  );
  var values = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var recordColumn = findHeaderIndex_(header, ["레코드 ID", "Record ID"]);
  var scoredPercent =
    record.scoredPercent != null ? record.scoredPercent : record.initialPercent;
  var scoredRank =
    record.scoredRank != null ? record.scoredRank : record.registeredRank;
  var scoredMinimumRecord =
    record.scoredMinimumRecord != null
      ? record.scoredMinimumRecord
      : record.registeredMinimumRecord;
  var rowObject = {
    "레코드 ID": record.recordId,
    "맵 코드": record.levelId,
    "플레이어": record.player,
    "최고 기록(%)": record.percent,
    "클리어 시각": record.clearedAt,
    "시도 횟수": record.attempts,
    "점프": record.jumps,
    "플레이 시간(ms)": record.playTimeMs,
    "플랫폼": record.platform,
    "상태": record.status,
    "증거": record.proofUrl,
    "모드 버전": record.modVersion,
    "점수 반영 기록(%)": scoredPercent,
    "점수 반영 순위": scoredRank,
    "점수 반영 최소 기록": scoredMinimumRecord,
    "확정 점수": record.score,
    "점수 공식 버전": record.scoringVersion,
    "점수 확정 시각": record.scoreLockedAt,
    "엔드스크린 증거 ID": record.evidenceId,
    "엔드스크린 파일 URL": record.evidenceUrl,
  };
  var publicRow = header.map(function (columnName) {
    return Object.prototype.hasOwnProperty.call(rowObject, columnName) ? rowObject[columnName] : "";
  });

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    if (String(values[rowIndex][recordColumn]).trim() !== String(record.recordId)) continue;
    sheet.getRange(rowIndex + 1, 1, 1, publicRow.length).setValues([publicRow]);
    return;
  }

  sheet.appendRow(publicRow);
}

function syncPublicClearRecords_(records) {
  if (!Array.isArray(records) || records.length === 0) return;

  var sheet = getOrCreateSheet_(
    getSpreadsheet_(),
    CORUM_SHEETS.publicClears,
    CORUM_PUBLIC_CLEAR_HEADERS,
  );
  var width = sheet.getLastColumn();
  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var rows = values.slice(1);
  var recordColumn = findHeaderIndex_(header, ["레코드 ID", "Record ID"]);
  var rowIndexes = {};

  rows.forEach(function (row, rowIndex) {
    var recordId = String(row[recordColumn] || "").trim();
    if (recordId && !Object.prototype.hasOwnProperty.call(rowIndexes, recordId)) {
      rowIndexes[recordId] = rowIndex;
    }
  });

  records.forEach(function (record) {
    var scoredPercent =
      record.scoredPercent != null ? record.scoredPercent : record.initialPercent;
    var scoredRank =
      record.scoredRank != null ? record.scoredRank : record.registeredRank;
    var scoredMinimumRecord =
      record.scoredMinimumRecord != null
        ? record.scoredMinimumRecord
        : record.registeredMinimumRecord;
    var rowObject = {
      "레코드 ID": record.recordId,
      "맵 코드": record.levelId,
      "플레이어": record.player,
      "최고 기록(%)": record.percent,
      "클리어 시각": record.clearedAt,
      "시도 횟수": record.attempts,
      "점프": record.jumps,
      "플레이 시간(ms)": record.playTimeMs,
      "플랫폼": record.platform,
      "상태": record.status,
      "증거": record.proofUrl,
      "모드 버전": record.modVersion,
      "점수 반영 기록(%)": scoredPercent,
      "점수 반영 순위": scoredRank,
      "점수 반영 최소 기록": scoredMinimumRecord,
      "확정 점수": record.score,
      "점수 공식 버전": record.scoringVersion,
      "점수 확정 시각": record.scoreLockedAt,
      "엔드스크린 증거 ID": record.evidenceId,
      "엔드스크린 파일 URL": record.evidenceUrl,
    };
    var publicRow = objectRow_(header, rowObject);
    var recordId = String(record.recordId || "").trim();

    if (recordId && Object.prototype.hasOwnProperty.call(rowIndexes, recordId)) {
      rows[rowIndexes[recordId]] = publicRow;
    } else {
      rows.push(publicRow);
      if (recordId) rowIndexes[recordId] = rows.length - 1;
    }
  });

  if (rows.length > 0) {
    sheet.getRange(2, 1, rows.length, width).setValues(rows);
  }
}

function getRecordsSheet_() {
  var spreadsheet = getSpreadsheet_();
  var existingSheet = spreadsheet.getSheetByName(CORUM_SHEETS.clears);
  if (existingSheet && existingSheet.getLastRow() > 0) {
    ensureRecordMapTitleColumn_(existingSheet);
  }
  return getOrCreateSheet_(spreadsheet, CORUM_SHEETS.clears, CORUM_CLEAR_HEADERS);
}

/**
 * Records의 맵 제목 열은 맵 코드 바로 오른쪽에 둔다.
 * v2.17 이하의 기존 시트에는 이 열이 없으므로 데이터 전체를 밀어 보존한다.
 */
function ensureRecordMapTitleColumn_(sheet) {
  if (!sheet || sheet.getLastColumn() < 1) return;

  var headers = sheet
    .getRange(1, 1, 1, sheet.getLastColumn())
    .getDisplayValues()[0];
  if (findOptionalHeaderIndex_(headers, ["맵 제목", "Map Title"]) !== -1) return;

  var levelColumn = findOptionalHeaderIndex_(headers, ["맵 코드", "Level ID"]);
  if (levelColumn === -1) return;

  var levelColumnNumber = levelColumn + 1;
  sheet.insertColumnAfter(levelColumnNumber);
  sheet.getRange(1, levelColumnNumber + 1).setValue("맵 제목");
}

function backfillRecordMapTitles_(sheet, maps) {
  if (!sheet || sheet.getLastRow() < 2) return 0;

  var headers = sheet
    .getRange(1, 1, 1, sheet.getLastColumn())
    .getDisplayValues()[0];
  var levelColumn = findOptionalHeaderIndex_(headers, ["맵 코드", "Level ID"]);
  var titleColumn = findOptionalHeaderIndex_(headers, ["맵 제목", "Map Title"]);
  if (levelColumn === -1 || titleColumn === -1) return 0;

  var mapsByLevelId = mapLookupByLevelId_(maps || readMaps_());
  var rowCount = sheet.getLastRow() - 1;
  var levelValues = sheet.getRange(2, levelColumn + 1, rowCount, 1).getValues();
  var titleRange = sheet.getRange(2, titleColumn + 1, rowCount, 1);
  var titleValues = titleRange.getValues();
  var changed = 0;

  for (var rowIndex = 0; rowIndex < rowCount; rowIndex += 1) {
    var rawLevelId = String(levelValues[rowIndex][0] || "").trim();
    var map = mapsByLevelId[rawLevelId];
    if (!map || !map.title || String(titleValues[rowIndex][0] || "") === map.title) {
      continue;
    }
    titleValues[rowIndex][0] = map.title;
    changed += 1;
  }

  if (changed > 0) titleRange.setValues(titleValues);
  return changed;
}

function getRecordSheets_() {
  var spreadsheet = getSpreadsheet_();
  var sheets = [
    getRecordsSheet_(),
  ];

  CORUM_LEGACY_CLEAR_SHEET_NAMES.forEach(function (sheetName) {
    if (sheetName === CORUM_SHEETS.clears) return;

    var legacySheet = spreadsheet.getSheetByName(sheetName);
    if (!legacySheet) return;

    sheets.push(getOrCreateSheet_(spreadsheet, sheetName, CORUM_CLEAR_HEADERS));
  });

  return sheets;
}

function mergeDuplicateClearRecords_(current, candidate) {
  if (!current) return candidate;

  var currentTimestamp = Date.parse(current.clearedAt) || 0;
  var candidateTimestamp = Date.parse(candidate.clearedAt) || 0;
  var primary = candidateTimestamp >= currentTimestamp ? candidate : current;
  var secondary = primary === candidate ? current : candidate;
  var merged = {};

  Object.keys(primary).forEach(function (key) {
    merged[key] = primary[key];
  });

  Object.keys(secondary).forEach(function (key) {
    if (merged[key] === "" || merged[key] == null) {
      merged[key] = secondary[key];
    }
  });

  return merged;
}

function readAllClearRecords_(existingMapLookup) {
  var recordsByKey = {};

  getRecordSheets_().forEach(function (sheet) {
    var values = sheet.getDataRange().getValues();
    if (values.length < 2) return;

    var header = values[0];

    for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
      var record = publicClearRecord_(header, values[rowIndex]);
      if (!record.levelId || !record.player) continue;

      var recordKey = record.recordId
        ? "record:" + record.recordId
        : [
            "row",
            sheet.getName(),
            rowIndex,
            record.levelId,
            record.accountId || record.player.toLowerCase(),
          ].join(":");

      recordsByKey[recordKey] = mergeDuplicateClearRecords_(
        recordsByKey[recordKey],
        record,
      );
    }
  });

  var mapsByLevelId =
    existingMapLookup || mapLookupByLevelId_(readMaps_());
  return Object.keys(recordsByKey).map(function (recordKey) {
    var record = recordsByKey[recordKey];
    var canonicalLevelId = canonicalLevelId_(record.levelId, mapsByLevelId);
    if (canonicalLevelId) record.levelId = canonicalLevelId;
    return record;
  });
}

function ensureClientVersionsSheet_() {
  var sheet = getOrCreateSheet_(
    getSpreadsheet_(),
    CORUM_SHEETS.clientVersions,
    CORUM_CLIENT_VERSION_HEADERS,
  );
  var existingPlatforms = {};

  if (sheet.getLastRow() >= 2) {
    var values = sheet.getDataRange().getDisplayValues();
    var platformColumn = findOptionalHeaderIndex_(
      values[0],
      ["플랫폼", "Platform"],
    );
    if (platformColumn !== -1) {
      for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
        var platform = normalizeClientPlatform_(values[rowIndex][platformColumn]);
        if (platform) existingPlatforms[platform] = true;
      }
    }
  }

  CORUM_CLIENT_VERSION_DEFAULTS.forEach(function (row) {
    var platform = normalizeClientPlatform_(row[0]);
    if (!platform || existingPlatforms[platform]) return;
    sheet.appendRow(Array.prototype.slice.call(row));
    existingPlatforms[platform] = true;
  });
  migrateAndroidV100ClientPolicy_(sheet);
  migrateV102ClientPolicies_(sheet);
  return sheet;
}

/**
 * v1.0.0 통합 출시 전에 생성된 기본 Android 정책만 새 정식 버전으로 올린다.
 * 운영자가 다른 버전으로 직접 바꾼 행은 덮어쓰지 않는다.
 */
function migrateAndroidV100ClientPolicy_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return;

  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var platformColumn = findOptionalHeaderIndex_(header, ["플랫폼", "Platform"]);
  var minimumColumn = findOptionalHeaderIndex_(
    header,
    ["최소 지원 버전", "Minimum Supported Version", "Minimum Version"],
  );
  var latestColumn = findOptionalHeaderIndex_(
    header,
    ["최신 버전", "Latest Version"],
  );
  if (platformColumn === -1 || minimumColumn === -1) return;

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    if (normalizeClientPlatform_(row[platformColumn]) !== "android") continue;

    var minimum = String(row[minimumColumn] == null ? "" : row[minimumColumn]).trim();
    var latest = latestColumn === -1
      ? ""
      : String(row[latestColumn] == null ? "" : row[latestColumn]).trim();
    if (minimum !== "v0.2.40") continue;
    if (latest && latest !== "v0.2.40" && latest !== "v1.0.0") continue;

    sheet.getRange(rowIndex + 1, minimumColumn + 1).setValue("v1.0.0");
    if (latestColumn !== -1) {
      sheet.getRange(rowIndex + 1, latestColumn + 1).setValue("v1.0.0");
    }
  }
}

/**
 * v1.0.0 또는 중간 v1.0.1 기본 정책을 사용 중인 플랫폼을 v1.0.2로 올린다.
 * iOS는 v1.0.2에서 처음 지원되므로 최소 지원 버전도 함께 올린다.
 * 운영자가 직접 수정한 정책 행은 덮어쓰지 않는다.
 */
function migrateV102ClientPolicies_(sheet) {
  if (!sheet || sheet.getLastRow() < 2) return;

  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var platformColumn = findOptionalHeaderIndex_(header, ["플랫폼", "Platform"]);
  var minimumColumn = findOptionalHeaderIndex_(
    header,
    ["최소 지원 버전", "Minimum Supported Version", "Minimum Version"],
  );
  var latestColumn = findOptionalHeaderIndex_(
    header,
    ["최신 버전", "Latest Version"],
  );
  if (platformColumn === -1 || minimumColumn === -1 || latestColumn === -1) {
    return;
  }

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var platform = normalizeClientPlatform_(row[platformColumn]);
    var minimum = String(row[minimumColumn] == null ? "" : row[minimumColumn]).trim();
    var latest = String(row[latestColumn] == null ? "" : row[latestColumn]).trim();
    if (platform === "windows" || platform === "android") {
      if (minimum !== "v1.0.0") continue;
      if (latest !== "v1.0.0" && latest !== "v1.0.1") continue;
      sheet.getRange(rowIndex + 1, latestColumn + 1).setValue("v1.0.2");
    } else if (platform === "ios") {
      var isV100Default = minimum === "v1.0.0" && latest === "v1.0.0";
      var isV101Default = minimum === "v1.0.1" && latest === "v1.0.1";
      if (!isV100Default && !isV101Default) continue;
      sheet.getRange(rowIndex + 1, minimumColumn + 1).setValue("v1.0.2");
      sheet.getRange(rowIndex + 1, latestColumn + 1).setValue("v1.0.2");
    }
  }
}

function normalizeClientPlatform_(value) {
  var normalized = String(value == null ? "" : value)
    .trim()
    .toLowerCase()
    .replace(/[\s_-]+/g, "");
  if (normalized.indexOf("win") === 0) return "windows";
  if (normalized.indexOf("android") === 0) return "android";
  if (
    normalized.indexOf("ios") === 0 ||
    normalized.indexOf("iphone") === 0 ||
    normalized.indexOf("ipad") === 0
  ) {
    return "ios";
  }
  return "";
}

function parseSemanticVersion_(value) {
  var match = String(value == null ? "" : value)
    .trim()
    .match(
      /^[vV]?(\d+)\.(\d+)\.(\d+)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$/,
    );
  if (!match) return null;

  var numbers = [Number(match[1]), Number(match[2]), Number(match[3])];
  if (
    numbers.some(function (number) {
      return !Number.isSafeInteger(number) || number < 0;
    })
  ) {
    return null;
  }
  return {
    numbers: numbers,
    prerelease: match[4] || "",
  };
}

function compareSemanticVersions_(left, right) {
  for (var index = 0; index < 3; index += 1) {
    if (left.numbers[index] === right.numbers[index]) continue;
    return left.numbers[index] < right.numbers[index] ? -1 : 1;
  }

  if (!left.prerelease && !right.prerelease) return 0;
  if (!left.prerelease) return 1;
  if (!right.prerelease) return -1;

  var leftParts = left.prerelease.split(".");
  var rightParts = right.prerelease.split(".");
  var commonLength = Math.min(leftParts.length, rightParts.length);
  for (var partIndex = 0; partIndex < commonLength; partIndex += 1) {
    var leftPart = leftParts[partIndex];
    var rightPart = rightParts[partIndex];
    if (leftPart === rightPart) continue;

    var leftNumeric = /^\d+$/.test(leftPart);
    var rightNumeric = /^\d+$/.test(rightPart);
    if (leftNumeric && rightNumeric) {
      var leftNumber = Number(leftPart);
      var rightNumber = Number(rightPart);
      if (leftNumber !== rightNumber) return leftNumber < rightNumber ? -1 : 1;
    } else if (leftNumeric !== rightNumeric) {
      return leftNumeric ? -1 : 1;
    }
    return leftPart < rightPart ? -1 : 1;
  }

  if (leftParts.length === rightParts.length) return 0;
  return leftParts.length < rightParts.length ? -1 : 1;
}

function clientVersionBoolean_(value, fallback) {
  if (typeof value === "boolean") return value;
  var normalized = String(value == null ? "" : value).trim().toLowerCase();
  if (["true", "1", "yes", "y", "on", "사용", "활성"].indexOf(normalized) !== -1) {
    return true;
  }
  if (["false", "0", "no", "n", "off", "미사용", "비활성"].indexOf(normalized) !== -1) {
    return false;
  }
  return fallback;
}

function safeClientUpdateUrl_(value) {
  var url = String(value == null ? "" : value).trim();
  var releasePrefix = "https://github.com/ybaf100/Corum-integration/releases";
  return url === releasePrefix || url.indexOf(releasePrefix + "/") === 0
    ? url
    : "";
}

function readClientVersionPolicies_() {
  var sheet = getSpreadsheet_().getSheetByName(CORUM_SHEETS.clientVersions);
  if (!sheet || sheet.getLastRow() < 2 || sheet.getLastColumn() < 1) return {};

  var values = sheet.getDataRange().getValues();
  var header = values[0];
  var platformColumn = findOptionalHeaderIndex_(header, ["플랫폼", "Platform"]);
  var minimumColumn = findOptionalHeaderIndex_(
    header,
    ["최소 지원 버전", "Minimum Supported Version", "Minimum Version"],
  );
  var latestColumn = findOptionalHeaderIndex_(
    header,
    ["최신 버전", "Latest Version"],
  );
  var updateColumn = findOptionalHeaderIndex_(
    header,
    ["업데이트 URL", "Update URL", "Download URL"],
  );
  var enabledColumn = findOptionalHeaderIndex_(
    header,
    ["버전 검사 활성", "Enforcement Enabled", "Enabled"],
  );
  if (platformColumn === -1 || minimumColumn === -1) return {};

  var policies = {};
  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var row = values[rowIndex];
    var platform = normalizeClientPlatform_(row[platformColumn]);
    var minimum = String(row[minimumColumn] == null ? "" : row[minimumColumn]).trim();
    if (!platform || !parseSemanticVersion_(minimum)) continue;

    var latest = latestColumn === -1
      ? minimum
      : String(row[latestColumn] == null ? "" : row[latestColumn]).trim();
    if (!parseSemanticVersion_(latest)) latest = minimum;

    policies[platform] = {
      minimumSupportedVersion: minimum,
      latestVersion: latest,
      updateUrl: updateColumn === -1
        ? ""
        : safeClientUpdateUrl_(row[updateColumn]),
      enforcementEnabled: enabledColumn === -1
        ? true
        : clientVersionBoolean_(row[enabledColumn], true),
    };
  }
  return policies;
}

function clientVersionRejection_(body) {
  var policies = readClientVersionPolicies_();
  var policyKeys = Object.keys(policies);
  if (policyKeys.length === 0) return null;

  var platform = normalizeClientPlatform_(body && body.platform);
  var policy = policies[platform];
  if (!platform || !policy) {
    return json_({
      ok: false,
      error: {
        code: "CLIENT_PLATFORM_UNSUPPORTED",
        message: "이 플랫폼에서는 Corum 기록을 제출할 수 없습니다.",
      },
    });
  }
  if (!policy.enforcementEnabled) return null;

  var current = parseSemanticVersion_(body && body.modVersion);
  var minimum = parseSemanticVersion_(policy.minimumSupportedVersion);
  if (!current || !minimum) {
    return json_({
      ok: false,
      error: {
        code: "CLIENT_VERSION_REQUIRED",
        message: "C Integration 버전을 확인할 수 없습니다.",
        platform: platform,
        minimumSupportedVersion: policy.minimumSupportedVersion,
        latestVersion: policy.latestVersion,
        updateUrl: policy.updateUrl,
      },
    });
  }

  if (compareSemanticVersions_(current, minimum) >= 0) return null;
  return json_({
    ok: false,
    error: {
      code: "CLIENT_OUTDATED",
      message: "지원이 종료된 C Integration 버전입니다. 업데이트 후 다시 시도해 주세요.",
      platform: platform,
      currentVersion: String(body.modVersion || "").trim(),
      minimumSupportedVersion: policy.minimumSupportedVersion,
      latestVersion: policy.latestVersion,
      updateUrl: policy.updateUrl,
    },
  });
}

function getSpreadsheet_() {
  var spreadsheetId = PropertiesService.getScriptProperties().getProperty("SPREADSHEET_ID");
  if (spreadsheetId) return SpreadsheetApp.openById(spreadsheetId);

  var activeSpreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  if (!activeSpreadsheet) {
    throw new Error("SPREADSHEET_ID 스크립트 속성을 설정해야 합니다.");
  }
  return activeSpreadsheet;
}

function getMapsSheet_() {
  var spreadsheet = getSpreadsheet_();
  var configuredName = PropertiesService.getScriptProperties().getProperty("MAPS_SHEET_NAME");
  if (configuredName) {
    var configuredSheet = spreadsheet.getSheetByName(configuredName);
    if (!configuredSheet) throw new Error("설정한 맵 시트를 찾을 수 없습니다: " + configuredName);
    return configuredSheet;
  }

  var sheets = spreadsheet.getSheets();
  for (var index = 0; index < sheets.length; index += 1) {
    var name = sheets[index].getName();
    if (
      name !== CORUM_SHEETS.players &&
      name !== CORUM_SHEETS.clears &&
      name !== CORUM_SHEETS.publicClears &&
      name !== CORUM_SHEETS.csmpTiers &&
      name !== CORUM_SHEETS.verifierRecords &&
      name !== CORUM_SHEETS.clearEvidence &&
      name !== CORUM_SHEETS.clientVersions &&
      CORUM_LEGACY_CLEAR_SHEET_NAMES.indexOf(name) === -1
    ) {
      return sheets[index];
    }
  }

  throw new Error("맵 데이터 시트를 찾을 수 없습니다.");
}

function scoreHeaderAliases_(header) {
  var aliases = CORUM_SCORE_HEADER_ALIASES[header];
  return aliases ? Array.prototype.slice.call(aliases) : [header];
}

function normalizeScoreSnapshotHeaders_(sheet) {
  if (sheet.getLastRow() === 0 || sheet.getLastColumn() === 0) return;

  var headers = sheet
    .getRange(1, 1, 1, sheet.getLastColumn())
    .getDisplayValues()[0];

  Object.keys(CORUM_SCORE_HEADER_ALIASES).forEach(function (canonicalHeader) {
    if (findOptionalHeaderIndex_(headers, [canonicalHeader]) !== -1) return;

    var aliases = scoreHeaderAliases_(canonicalHeader).slice(1);
    var oldColumn = findOptionalHeaderIndex_(headers, aliases);
    if (oldColumn === -1) return;

    sheet.getRange(1, oldColumn + 1).setValue(canonicalHeader);
    headers[oldColumn] = canonicalHeader;
  });
}

function getOrCreateSheet_(spreadsheet, name, headers) {
  var sheet = spreadsheet.getSheetByName(name);
  if (!sheet) sheet = spreadsheet.insertSheet(name);

  if (sheet.getLastRow() === 0) {
    sheet.getRange(1, 1, 1, headers.length).setValues([headers]);
    return sheet;
  }

  var existingHeaders = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  var missingHeaders = headers.filter(function (header) {
    return findOptionalHeaderIndex_(existingHeaders, scoreHeaderAliases_(header)) === -1;
  });

  if (missingHeaders.length > 0) {
    sheet
      .getRange(1, existingHeaders.length + 1, 1, missingHeaders.length)
      .setValues([missingHeaders]);
  }

  normalizeScoreSnapshotHeaders_(sheet);
  return sheet;
}

function ensureMapMinimumRecordColumn_() {
  var sheet = getMapsSheet_();
  var headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  var aliases = [
    "최소 등록 가능 기록",
    "최소 등록 기록",
    "Minimum Record",
    "Min Record",
  ];

  if (findOptionalHeaderIndex_(headers, aliases) !== -1) return;
  sheet.getRange(1, headers.length + 1).setValue("최소 등록 가능 기록");
}

function ensureMapAlternateLevelColumn_() {
  var sheet = getMapsSheet_();
  var headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  var aliases = [
    "대체 맵 코드",
    "대체맵코드",
    "Alternate Level ID",
    "Alternative Level ID",
    "Alt Level ID",
    "alternateLevelId",
  ];

  if (findOptionalHeaderIndex_(headers, aliases) !== -1) return;
  sheet.getRange(1, headers.length + 1).setValue("대체 맵 코드");
}

function ensureMapCsmpTierColumn_() {
  var sheet = getMapsSheet_();
  var headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  var aliases = [
    "CSMP 티어 배정",
    "CSMP 티어",
    "CSMP Tier Assignment",
    "CSMP Tier",
    "csmpTier",
  ];

  if (findOptionalHeaderIndex_(headers, aliases) !== -1) return false;
  sheet.getRange(1, headers.length + 1).setValue("CSMP 티어 배정");
  return true;
}

function seedDefaultCsmpMapAssignments_() {
  var sheet = getMapsSheet_();
  if (sheet.getLastRow() < 2) return;

  var width = sheet.getLastColumn();
  var values = sheet.getRange(1, 1, sheet.getLastRow(), width).getDisplayValues();
  var header = values[0];
  var titleColumn = findHeaderIndex_(header, ["맵 제목", "맵제목", "제목", "Title"]);
  var tierColumn = findHeaderIndex_(header, [
    "CSMP 티어 배정",
    "CSMP 티어",
    "CSMP Tier Assignment",
    "CSMP Tier",
    "csmpTier",
  ]);
  var assignments = [];

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var existing = String(values[rowIndex][tierColumn] || "").trim();
    var titleKey = normalizeDefaultCsmpMapTitle_(values[rowIndex][titleColumn]);
    assignments.push([
      existing || CORUM_CSMP_DEFAULT_MAP_TIERS[titleKey] || "",
    ]);
  }

  sheet.getRange(2, tierColumn + 1, assignments.length, 1).setValues(assignments);
}

function ensureCsmpTiersSheet_() {
  var spreadsheet = getSpreadsheet_();
  var sheet = spreadsheet.getSheetByName(CORUM_SHEETS.csmpTiers);

  if (!sheet) {
    sheet = spreadsheet.insertSheet(CORUM_SHEETS.csmpTiers);
    sheet
      .getRange(1, 1, 1, CORUM_CSMP_TIER_HEADERS.length)
      .setValues([Array.prototype.slice.call(CORUM_CSMP_TIER_HEADERS)]);
    sheet
      .getRange(2, 1, CORUM_CSMP_DEFAULT_TIERS.length, CORUM_CSMP_TIER_HEADERS.length)
      .setValues(CORUM_CSMP_DEFAULT_TIERS.map(function (row) {
        return Array.prototype.slice.call(row);
      }));
  } else if (sheet.getLastRow() === 0) {
    sheet
      .getRange(1, 1, 1, CORUM_CSMP_TIER_HEADERS.length)
      .setValues([Array.prototype.slice.call(CORUM_CSMP_TIER_HEADERS)]);
  } else {
    getOrCreateSheet_(spreadsheet, CORUM_SHEETS.csmpTiers, CORUM_CSMP_TIER_HEADERS);
  }

  sheet.setFrozenRows(1);
  return sheet;
}

function applyCsmpTierValidation_(csmpTiersSheet) {
  if (!SpreadsheetApp.newDataValidation) return;

  var mapsSheet = getMapsSheet_();
  var headers = mapsSheet.getRange(1, 1, 1, mapsSheet.getLastColumn()).getDisplayValues()[0];
  var tierColumn = findHeaderIndex_(headers, [
    "CSMP 티어 배정",
    "CSMP 티어",
    "CSMP Tier Assignment",
    "CSMP Tier",
    "csmpTier",
  ]);
  var maxRows = typeof mapsSheet.getMaxRows === "function"
    ? mapsSheet.getMaxRows()
    : mapsSheet.getLastRow();
  if (maxRows < 2) return;

  var targetRange = mapsSheet.getRange(2, tierColumn + 1, maxRows - 1, 1);
  if (typeof targetRange.clearDataValidations === "function") {
    targetRange.clearDataValidations();
  }

  if (csmpTiersSheet.getLastRow() < 2 || typeof targetRange.setDataValidation !== "function") {
    return;
  }

  var sourceRange = csmpTiersSheet.getRange(2, 2, csmpTiersSheet.getLastRow() - 1, 1);
  var builder = SpreadsheetApp.newDataValidation().requireValueInRange(sourceRange, true);
  if (typeof builder.setAllowInvalid === "function") builder.setAllowInvalid(true);
  targetRange.setDataValidation(builder.build());
}

function migrateLegacyRecordPercent_(sheet) {
  if (sheet.getLastRow() < 2) return;

  var values = sheet.getDataRange().getDisplayValues();
  var header = values[0];
  var recordColumn = findHeaderIndex_(header, ["레코드 ID", "Record ID"]);
  var percentColumn = findHeaderIndex_(header, ["최고 기록(%)", "기록(%)", "Record", "Percent"]);

  for (var rowIndex = 1; rowIndex < values.length; rowIndex += 1) {
    var hasRecord = String(values[rowIndex][recordColumn] || "").trim() !== "";
    var hasPercent = String(values[rowIndex][percentColumn] || "").trim() !== "";
    if (hasRecord && !hasPercent) {
      // v0.1은 100% 클리어만 저장했으므로 기존 행은 안전하게 100%로 이전한다.
      sheet.getRange(rowIndex + 1, percentColumn + 1).setValue(100);
    }
  }
}

function migrateLegacyScoreSnapshots_(sheet) {
  if (sheet.getLastRow() < 2) return 0;

  var mapsByLevelId = mapLookupByLevelId_(readMaps_());

  var width = sheet.getLastColumn();
  var header = sheet.getRange(1, 1, 1, width).getDisplayValues()[0];
  var recordColumn = findHeaderIndex_(header, ["레코드 ID", "Record ID"]);
  var levelColumn = findHeaderIndex_(header, ["맵 코드", "Level ID"]);
  var percentColumn = findHeaderIndex_(
    header,
    ["최고 기록(%)", "기록(%)", "Record", "Percent"],
  );
  var clearedAtColumn = findHeaderIndex_(header, ["클리어 시각", "Cleared At"]);
  var initialPercentColumn = findHeaderIndex_(
    header,
    [
      "점수 반영 기록(%)",
      "최초 등록 기록(%)",
      "최초 등록 기록",
      "Initial Record",
      "Scored Record",
    ],
  );
  var registeredRankColumn = findHeaderIndex_(
    header,
    [
      "점수 반영 순위",
      "최초 등록 순위",
      "등록 당시 순위",
      "Registered Rank",
      "Scored Rank",
    ],
  );
  var registeredMinimumColumn = findHeaderIndex_(
    header,
    [
      "점수 반영 최소 기록",
      "최초 등록 최소 기록",
      "등록 당시 최소 등록 기록",
      "Registered Minimum",
      "Scored Minimum",
    ],
  );
  var scoreColumn = findHeaderIndex_(
    header,
    ["확정 점수", "최초 등록 점수", "Frozen Score", "Awarded Score"],
  );
  var scoringVersionColumn = findHeaderIndex_(
    header,
    ["점수 공식 버전", "Scoring Version"],
  );
  var scoreLockedAtColumn = findHeaderIndex_(
    header,
    ["점수 확정 시각", "Score Locked At"],
  );

  var rowCount = sheet.getLastRow() - 1;
  var range = sheet.getRange(2, 1, rowCount, width);
  var rows = range.getValues();
  var displayRows = range.getDisplayValues();
  var changedRows = 0;

  for (var rowIndex = 0; rowIndex < displayRows.length; rowIndex += 1) {
    var displayRow = displayRows[rowIndex];
    var hasRecord = String(displayRow[recordColumn] || "").trim() !== "";
    var levelId = String(displayRow[levelColumn] || "").trim();
    var map = mapsByLevelId[levelId];
    if (!hasRecord || !map) continue;

    var currentPercent = parseRecordPercent_(displayRow[percentColumn]);
    if (!currentPercent) continue;

    var rowChanged = false;
    var initialPercent = positiveSnapshotNumber_(rows[rowIndex][initialPercentColumn]);
    if (!initialPercent || initialPercent > 100) {
      initialPercent = currentPercent;
      rows[rowIndex][initialPercentColumn] = initialPercent;
      rowChanged = true;
    }

    var registeredRank = positiveSnapshotNumber_(rows[rowIndex][registeredRankColumn]);
    if (!registeredRank) {
      registeredRank = map.rank;
      rows[rowIndex][registeredRankColumn] = registeredRank;
      rowChanged = true;
    }

    var registeredMinimum = positiveSnapshotNumber_(
      rows[rowIndex][registeredMinimumColumn],
    );
    if (!registeredMinimum || registeredMinimum > 100) {
      registeredMinimum = map.minimumRecord;
      rows[rowIndex][registeredMinimumColumn] = registeredMinimum;
      rowChanged = true;
    }

    var frozenScore = snapshotNumber_(rows[rowIndex][scoreColumn]);
    if (frozenScore === null || frozenScore < 0) {
      frozenScore = getCorumRecordScore_(
        registeredRank,
        initialPercent,
        registeredMinimum,
      );
      rows[rowIndex][scoreColumn] = frozenScore;
      rowChanged = true;
    }

    if (!String(rows[rowIndex][scoringVersionColumn] || "").trim()) {
      rows[rowIndex][scoringVersionColumn] = CORUM_SCORING_VERSION;
      rowChanged = true;
    }

    if (!String(rows[rowIndex][scoreLockedAtColumn] || "").trim()) {
      rows[rowIndex][scoreLockedAtColumn] =
        rows[rowIndex][clearedAtColumn] || new Date().toISOString();
      rowChanged = true;
    }

    if (rowChanged) changedRows += 1;
  }

  if (changedRows > 0) range.setValues(rows);
  return changedRows;
}

function appendObjectRow_(sheet, rowObject) {
  var headers = sheet.getRange(1, 1, 1, sheet.getLastColumn()).getDisplayValues()[0];
  sheet.appendRow(objectRow_(headers, rowObject));
}

function objectRow_(headers, rowObject) {
  return headers.map(function (header) {
    return Object.prototype.hasOwnProperty.call(rowObject, header)
      ? rowObject[header]
      : "";
  });
}

function applyObjectToRow_(headers, row, rowObject) {
  headers.forEach(function (header, index) {
    if (Object.prototype.hasOwnProperty.call(rowObject, header)) {
      row[index] = rowObject[header];
    }
  });
  return row;
}

function updateObjectRow_(sheet, rowNumber, rowObject) {
  var width = sheet.getLastColumn();
  var headers = sheet.getRange(1, 1, 1, width).getDisplayValues()[0];
  var row = sheet.getRange(rowNumber, 1, 1, width).getValues()[0];

  headers.forEach(function (header, index) {
    if (Object.prototype.hasOwnProperty.call(rowObject, header)) {
      row[index] = rowObject[header];
    }
  });

  sheet.getRange(rowNumber, 1, 1, width).setValues([row]);
}

function findHeaderIndex_(headers, aliases) {
  var index = findOptionalHeaderIndex_(headers, aliases);
  if (index !== -1) return index;
  throw new Error("필수 열을 찾을 수 없습니다: " + aliases.join(" / "));
}

function findOptionalHeaderIndex_(headers, aliases) {
  var normalizedAliases = aliases.map(normalizeHeader_);
  for (var index = 0; index < headers.length; index += 1) {
    if (normalizedAliases.indexOf(normalizeHeader_(headers[index])) !== -1) return index;
  }
  return -1;
}

function normalizeHeader_(value) {
  return String(value || "").replace(/^\uFEFF/, "").replace(/\s+/g, "").trim().toLowerCase();
}

function normalizeRating_(value) {
  var rating = String(value || "").trim();
  return /^20(?:\.0+)?$/.test(rating) ? "20.0" : rating;
}

function normalizeMinimumRecord_(value) {
  var text = String(value == null ? "" : value)
    .trim()
    .replace(/%$/, "")
    .trim();
  var number = Number(text);

  if (!text || !Number.isFinite(number) || number < 1 || number > 100) {
    return 100;
  }

  return number;
}

function normalizeAlternateLevelId_(value, primaryLevelId) {
  var alternateLevelId = String(value == null ? "" : value).trim();
  var canonicalLevelId = String(primaryLevelId == null ? "" : primaryLevelId).trim();

  if (
    !/^\d+$/.test(alternateLevelId) ||
    alternateLevelId === canonicalLevelId
  ) {
    return "";
  }

  return alternateLevelId;
}

function normalizeOptionalAccountId_(value) {
  var accountId = String(value == null ? "" : value).trim();
  if (!/^\d+$/.test(accountId) || /^0+$/.test(accountId)) return "";
  return accountId.replace(/^0+(?=\d)/, "");
}

function normalizePlayerKey_(value) {
  return String(value == null ? "" : value)
    .trim()
    .normalize("NFKC")
    .toLocaleLowerCase();
}

function normalizeCsmpTierName_(value) {
  return String(value || "").trim().toLocaleLowerCase();
}

function normalizeDefaultCsmpMapTitle_(value) {
  return String(value || "")
    .normalize("NFKC")
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/g, "");
}

function slugifyCsmpTierKey_(value) {
  return String(value || "")
    .normalize("NFKD")
    .toLocaleLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-+|-+$/g, "")
    .slice(0, 48);
}

function normalizeCsmpIconFile_(value, tierName, warnings) {
  var iconFile = String(value || "").trim();
  if (!iconFile) return "";
  if (
    iconFile.length > 120 ||
    iconFile.indexOf("/") !== -1 ||
    iconFile.indexOf("\\") !== -1 ||
    !/^[A-Za-z0-9_. -]+\.(?:png|jpe?g|webp)$/i.test(iconFile)
  ) {
    warnings.push("CSMP " + tierName + "의 아이콘 파일명이 올바르지 않아 아이콘을 비웠습니다.");
    return "";
  }
  return iconFile;
}

function normalizeCsmpColor_(value, tierName, warnings) {
  var color = String(value || "").trim().toUpperCase();
  if (/^#[0-9A-F]{6}$/.test(color)) return color;
  warnings.push("CSMP " + tierName + "의 대표 색상이 올바르지 않아 #888888을 사용합니다.");
  return "#888888";
}

function parseCsmpRequirement_(value, tierName, warnings) {
  var text = String(value == null ? "" : value).trim();
  if (!text || text.toLocaleLowerCase() === "all") {
    if (!text) {
      warnings.push("CSMP " + tierName + "의 최소 클리어 맵 수가 비어 있어 All로 처리했습니다.");
    }
    return { requiresAll: true, requiredCount: 0 };
  }

  var number = Number(text);
  if (!Number.isInteger(number) || number <= 0) {
    warnings.push("CSMP " + tierName + "의 최소 클리어 맵 수가 올바르지 않아 All로 처리했습니다.");
    return { requiresAll: true, requiredCount: 0 };
  }

  return { requiresAll: false, requiredCount: number };
}

function parseRecordPercent_(value) {
  var text = String(value == null ? "" : value)
    .trim()
    .replace(/%$/, "")
    .trim();
  var number = Number(text);

  if (!text || !Number.isFinite(number) || number < 1 || number > 100) {
    return 0;
  }

  return number;
}

function tierForRank_(rank) {
  if (!rank) return "Unranked";
  if (rank <= 5) return "TOP " + rank;
  if (rank <= 10) return "Main";
  if (rank <= 25) return "Extended";
  return "Legacy";
}

function requirePositiveInteger_(value, label) {
  var number = Number(value);
  if (!Number.isInteger(number) || number <= 0) {
    throw new Error(label + " 값이 올바르지 않습니다.");
  }
  return number;
}

function boundedInteger_(value, minimum, maximum, label) {
  var number = Number(value);
  if (!Number.isInteger(number) || number < minimum || number > maximum) {
    throw new Error(label + " 값이 올바르지 않습니다.");
  }
  return number;
}

function requireShortText_(value, label, maximumLength) {
  var text = String(value || "").trim();
  if (!text || text.length > maximumLength) {
    throw new Error(label + " 값이 올바르지 않습니다.");
  }
  return text;
}

function normalizeLoadedMods_(value) {
  if (!Array.isArray(value)) {
    return { present: false, text: "" };
  }

  var seen = {};
  var entries = [];
  var maximumEntries = Math.min(value.length, 128);

  for (var index = 0; index < maximumEntries; index += 1) {
    var entry = String(value[index] == null ? "" : value[index]).trim();
    if (
      !entry ||
      entry.length > 160 ||
      !/^[A-Za-z0-9][A-Za-z0-9._-]{0,94}(?:@v?[A-Za-z0-9][A-Za-z0-9.+_-]{0,62})?$/.test(entry)
    ) {
      continue;
    }

    var key = entry.toLocaleLowerCase();
    if (seen[key]) continue;
    seen[key] = true;
    entries.push(entry);
  }

  return {
    present: true,
    text: entries.join(", "),
  };
}

function safeText_(value, maximumLength) {
  return String(value || "").trim().slice(0, maximumLength);
}

function safeClientTimestamp_(value) {
  var numericValue = Number(value);
  if (!Number.isFinite(numericValue) || numericValue <= 0) return "";
  var date = new Date(numericValue);
  return Number.isNaN(date.getTime()) ? "" : date.toISOString();
}

function jsonError_(code, message) {
  return json_({
    ok: false,
    error: {
      code: code,
      message: message,
    },
  });
}

function json_(payload) {
  return ContentService
    .createTextOutput(JSON.stringify(payload))
    .setMimeType(ContentService.MimeType.JSON);
}
