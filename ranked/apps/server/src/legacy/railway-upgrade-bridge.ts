import { createServer, type IncomingMessage, type ServerResponse } from "node:http";

const LIGHTSAIL_CONFIG_URL = "https://api.3-34-236-183.sslip.io/api/ranked/config";
const LEGACY_PROJECT_ID = "9a50ad5f-c812-4200-b97a-0296492097cd";
const LEGACY_SERVICE_ID = "f8f77cca-8054-47bb-b7e7-21581f394f0f";

/** Retired Railway service is upgrade-only; never initialize AppModule or Neon. */
export const shouldStartLegacyRailwayBridge = (env: NodeJS.ProcessEnv): boolean =>
  env.RAILWAY_PROJECT_ID === LEGACY_PROJECT_ID &&
  env.RAILWAY_SERVICE_ID === LEGACY_SERVICE_ID;

const json = (res: ServerResponse, code: number, data: object): void => {
  res.writeHead(code, {
    "content-type": "application/json; charset=utf-8",
    "cache-control": "no-store",
    "access-control-allow-origin": "*",
  });
  res.end(JSON.stringify(data));
};

export const createLegacyRailwayHandler = (fetchConfig: typeof fetch = fetch) =>
  async (req: IncomingMessage, res: ServerResponse): Promise<void> => {
    const path = new URL(req.url ?? "/", "http://localhost").pathname;
    if (req.method === "GET" && (path === "/health" || path === "/" || path === "/ready")) {
      json(res, 200, { status: "ok", service: "corum-ranked-legacy-upgrade-bridge" });
      return;
    }
    if (req.method === "GET" && path === "/api/ranked/config") {
      try {
        const upstream = await fetchConfig(LIGHTSAIL_CONFIG_URL, {
          method: "GET",
          headers: { accept: "application/json" },
          signal: AbortSignal.timeout(8000),
        });
        if (!upstream.ok) throw new Error("Lightsail config unavailable");
        const content = await upstream.text();
        if (Buffer.byteLength(content, "utf8") > 128 * 1024) {
          throw new Error("Invalid config response length");
        }
        const data: unknown = JSON.parse(content);
        if (typeof data !== "object" || data === null ||
          !("requiredClientVersion" in data) || !("modUpdates" in data) ||
          typeof data.requiredClientVersion !== "string" ||
          typeof data.modUpdates !== "object" || data.modUpdates === null) {
          throw new Error("Missing update descriptors");
        }
        res.writeHead(200, {
          "content-type": "application/json; charset=utf-8",
          "cache-control": "no-store",
          "access-control-allow-origin": "*",
        });
        res.end(content);
      } catch {
        json(res, 503, {
          code: "UPDATE_BRIDGE_UNAVAILABLE",
          message: "Please retry the Corum Ranked update shortly.",
        });
      }
      return;
    }
    // Old clients cannot start matches, settle scores or modify Neon through Railway.
    json(res, 426, {
      code: "RANKED_CLIENT_UPDATE_REQUIRED",
      requiredVersion: "v0.5.0-beta.93",
      message: "Update Corum Ranked to the Lightsail edition.",
    });
  };

export const startLegacyRailwayBridge = async (): Promise<void> => {
  const port = Number(process.env.PORT || "3000");
  if (!Number.isSafeInteger(port) || port < 1 || port > 65535) {
    throw new Error("Invalid Railway port for upgrade bridge");
  }
  const server = createServer(createLegacyRailwayHandler());
  await new Promise<void>((resolve, reject) => {
    server.once("error", reject);
    server.listen(port, "0.0.0.0", resolve);
  });
  console.log("Legacy Railway upgrade-only bridge active, with no database connection");
};
