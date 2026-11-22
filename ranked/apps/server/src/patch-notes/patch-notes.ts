// Player-facing patch notes, newest first. Each release adds one entry with
// the same lines in English and Korean (the client shows either). Plain text
// only; the client draws it with the system font, so any Korean is allowed.
export interface PatchNote {
  readonly version: string;
  readonly date: string;
  readonly en: readonly string[];
  readonly ko: readonly string[];
}

export const PATCH_NOTES: readonly PatchNote[] = Object.freeze([
  {
    version: "0.6.0-beta.91", date: "2026-11-22",
    en: [
      "Debug: the DEBUG menu now lists Bot Match, Spectate, Admin and Fixed Map Battle. The fixed-map screen uses the Corum style, and the bot difficulty is chosen in its opponent picker.",
    ],
    ko: [
      "디버그: DEBUG 메뉴에 봇 매치, 관전, Admin, 지정맵 매치가 나옵니다. 지정맵 화면을 Corum 스타일로 바꾸고, 봇 난이도는 상대 선택에서 고릅니다.",
    ],
  },
  {
    version: "0.6.0-beta.90", date: "2026-11-22",
    en: [
      "Debug: fixed-map battles. Pick R1, R2, R3 and the Death Match map from the map pool and play them against the Debug Bot, or send them to a debug player in the lobby (that match counts for LP). No ban phase; empty slots are random.",
    ],
    ko: [
      "디버그: 지정맵 배틀. 맵 풀에서 R1, R2, R3, 데스매치 맵을 골라 디버그 봇과 하거나, 로비에 있는 디버그 플레이어에게 보낼 수 있습니다 (이 경기는 LP에 반영). 밴 단계는 없고 빈 칸은 무작위입니다.",
    ],
  },
  {
    version: "0.6.0-beta.89", date: "2026-11-20",
    en: [
      "Update screen: the guide now has 8 cards; the first shows your version > the new version with every change in between (the newest 5 releases).",
    ],
    ko: [
      "업데이트 화면: 안내 카드가 8장이 되었고, 첫 카드에 현재 버전 > 새 버전과 그 사이의 변경사항을 모두 보여줍니다 (최근 5개 버전).",
    ],
  },
  {
    version: "0.6.0-beta.88", date: "2026-11-20",
    en: [
      "Patch notes: open them from Settings, and they show first while an update downloads (English or Korean).",
    ],
    ko: [
      "패치 노트: 설정에서 열 수 있고, 업데이트를 기다리는 화면에서는 가장 먼저 표시됩니다 (한국어/영어).",
    ],
  },
  {
    version: "0.6.0-beta.87", date: "2026-11-20",
    en: [
      "PC keyboard: Enter/Space start the queue, queue again, play again and claim all achievements.",
      "PC keyboard: Esc cancels matching; 1-9 ban the map in that place, 0 is no ban; H opens the match history from a result.",
      "Each of those buttons shows its key.",
    ],
    ko: [
      "PC 키보드: Enter/Space로 대기열 시작, 다시 대기열, 다시하기, 도전과제 전체 수령을 할 수 있습니다.",
      "PC 키보드: Esc로 매칭 취소, 1~9로 해당 위치의 맵 밴, 0으로 밴 안 함, 결과 화면에서 H로 경기 기록을 엽니다.",
      "해당 버튼에는 키가 표시됩니다.",
    ],
  },
  {
    version: "0.6.0-beta.86", date: "2026-11-20",
    en: [
      "Every tier promotion (for example BRONZE I to SILVER III) now plays the full promotion motion, not only GOLD.",
      "Survival replays are now recorded for restarted attempts, not only the first attempt on a map.",
    ],
    ko: [
      "모든 티어 승급(예: 브론즈 I → 실버 III)에서 승급 연출이 재생됩니다 (기존에는 골드만).",
      "서바이벌 리플레이가 재시작한 시도에도 녹화됩니다 (기존에는 맵의 첫 시도만).",
    ],
  },
  {
    version: "0.6.0-beta.82", date: "2026-11-19",
    en: [
      "Replays: old unconfirmed recordings no longer fill the replay storage and block new recordings.",
      "Solo result: the top band draws in, an achievements box is always shown, and the mouse wheel no longer hides the rounds.",
      "Tier guide: Survival promotion series and the Solo MASTER / CHAMPION rules.",
      "History (all records): cards name the players of a match of up to two.",
      "PC: keys 1 / 2 open Survival / Solo from the main lobby.",
    ],
    ko: [
      "리플레이: 확인되지 않은 오래된 녹화가 저장 공간을 채워 새 녹화를 막던 문제를 수정했습니다.",
      "솔로 결과: 상단 띠 등장 연출, 도전과제 칸 상시 표시, 마우스 휠로 라운드 기록이 사라지던 문제 수정.",
      "티어 정보: 서바이벌 승격전 정보와 솔로 MASTER / CHAMPION 규칙을 추가했습니다.",
      "기록(모든 기록): 2명 이하 경기 카드에 플레이어 이름을 표시합니다.",
      "PC: 메인 로비에서 1 / 2 키로 서바이벌 / 솔로에 입장합니다.",
    ],
  },
  {
    version: "0.6.0-beta.81", date: "2026-11-19",
    en: ["Sharper text at 1080p: small text uses a font atlas sized for it."],
    ko: ["1080p에서 글씨가 더 선명해졌습니다: 작은 글씨는 그 크기에 맞는 폰트를 씁니다."],
  },
]);

/** The newest `limit` entries (all when omitted). */
export function patchNotes(limit = 20): readonly PatchNote[] {
  return PATCH_NOTES.slice(0, Math.max(0, Math.min(limit, PATCH_NOTES.length)));
}
