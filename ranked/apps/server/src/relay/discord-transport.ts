import { Injectable } from "@nestjs/common";

export const DISCORD_TRANSPORT = Symbol("DISCORD_TRANSPORT");

export interface DiscordTransport {
  send(webhookUrl: string, content: string, timeoutMs: number): Promise<void>;
}

@Injectable()
export class FetchDiscordTransport implements DiscordTransport {
  public async send(webhookUrl: string, content: string, timeoutMs: number): Promise<void> {
    const response = await fetch(webhookUrl, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({
        content,
        allowed_mentions: { parse: [] },
      }),
      signal: AbortSignal.timeout(timeoutMs),
    });
    if (!response.ok) {
      throw new Error(`Discord webhook returned HTTP ${response.status}`);
    }
  }
}

