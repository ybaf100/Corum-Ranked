import type { RankedRelayEventType } from "./outbox.service.js";

export interface RelayEventForFormatting {
  readonly eventType: RankedRelayEventType;
  readonly payload: unknown;
}

const objectValue = (value: unknown): Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value)
    ? (value as Record<string, unknown>)
    : {};

const text = (value: unknown, fallback = "?"): string => {
  const result = typeof value === "string" ? value : fallback;
  return result.replace(/[\r\n`]/g, " ").trim().slice(0, 120) || fallback;
};

const number = (value: unknown): number =>
  typeof value === "number" && Number.isFinite(value) ? value : 0;

const players = (payload: Record<string, unknown>): { A: string; B: string } => {
  const value = objectValue(payload.players);
  return { A: text(value.A, "Player A"), B: text(value.B, "Player B") };
};

const sideName = (side: unknown, names: { A: string; B: string }): string =>
  side === "A" ? names.A : side === "B" ? names.B : "Draw";

const scoreLine = (payload: Record<string, unknown>): string => {
  const scores = objectValue(payload.scores);
  const clears = objectValue(payload.clears);
  return `Score: ${number(scores.A)} (${number(clears.A)} clears) - ${number(scores.B)} (${number(clears.B)} clears)`;
};

const roundResults = (value: unknown): string =>
  Array.isArray(value) ? value.map((result) => text(result)).join(" / ") : "";

export const formatDiscordRelayMessage = (event: RelayEventForFormatting): string => {
  const payload = objectValue(event.payload);
  const names = players(payload);
  const match = `Match ${text(payload.matchId, "unknown")}`;
  let content: string;

  switch (event.eventType) {
    case "ROUND_START": {
      const banner = text(payload.banner, "NONE");
      const bannerText = banner === "NONE" ? "" : ` · ${banner.replaceAll("_", " ")}`;
      content = [
        `🎮 **Round ${number(payload.roundNumber)} Start${bannerText}**`,
        `${names.A} vs ${names.B} · ${match}`,
        `Map: **${text(payload.mapTitle)}** · Qualifying ${number(payload.qualifyingPercent)}%`,
      ].join("\n");
      break;
    }
    case "CLEAR_EVENT":
      content = [
        `✅ **${sideName(payload.side, names)} cleared** ${text(payload.mapTitle, "the map")}`,
        `${scoreLine(payload)} · ${match}`,
      ].join("\n");
      break;
    case "LAST_ATTEMPT":
      content = [
        `⏱️ **LAST ATTEMPT: ${sideName(payload.targetSide, names)}**`,
        `${number(payload.windowSeconds)}s attempt-start window · ${match}`,
      ].join("\n");
      break;
    case "ROUND_RESULT": {
      const result = payload.result === "DRAW" ? "Draw" : sideName(payload.result, names);
      const wins = objectValue(payload.roundWins);
      content = [
        `🏁 **Round ${number(payload.roundNumber)} Result: ${result}**`,
        `Match score: ${names.A} ${number(wins.A)} - ${number(wins.B)} ${names.B} · ${match}`,
      ].join("\n");
      break;
    }
    case "MATCH_RESULT": {
      const deltas = objectValue(payload.mmrDelta);
      const ratings = objectValue(payload.ratingAfter);
      content = payload.debugBotMatch === true
        ? [
            `🏆 **Debug Match Winner: ${sideName(payload.winnerSide, names)}**`,
            `MMR, placement, and public statistics were not applied.`,
            `Rounds: ${roundResults(payload.roundResults)} · ${match}`,
          ].join("\n")
        : [
            `🏆 **Match Winner: ${sideName(payload.winnerSide, names)}**`,
            `${names.A} ${number(deltas.A) >= 0 ? "+" : ""}${number(deltas.A)} → ${number(ratings.A)} · ` +
              `${names.B} ${number(deltas.B) >= 0 ? "+" : ""}${number(deltas.B)} → ${number(ratings.B)}`,
            `Rounds: ${roundResults(payload.roundResults)} · ${match}`,
          ].join("\n");
      break;
    }
    case "DEATHMATCH_START":
      content = [
        `⚔️ **Deathmatch ${number(payload.sequence)} Start**`,
        `${names.A} vs ${names.B} · exactly 3 attempts each`,
        `Map: **${text(payload.mapTitle)}** · Qualifying ${number(payload.qualifyingPercent)}% · ${match}`,
      ].join("\n");
      break;
    case "DEATHMATCH_RESULT": {
      const result = payload.repeatRequired
        ? "Tie — another map will be drawn"
        : sideName(payload.winnerSide, names);
      content = [
        `⚔️ **Deathmatch ${number(payload.sequence)} Result: ${result}**`,
        `Score: ${names.A} ${number(payload.scoreA)} - ${number(payload.scoreB)} ${names.B} · ${match}`,
      ].join("\n");
      break;
    }
  }

  if (payload.debugBotMatch === true) content = `🧪 **DEBUG BOT MATCH**\n${content}`;
  return content.slice(0, 1_900);
};
