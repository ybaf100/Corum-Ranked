// Paste this complete ES module into the corum-ranked-gd-relay Worker.
// Only public GD search/download data is forwarded; match authority stays in NestJS.
const GD_ORIGIN = "https://www.boomlings.com/database/";
const SEARCH = "/getGJLevels21.php", DOWNLOAD = "/downloadGJLevel22.php";
const UPSTREAM_HEADERS = ["server", "content-type", "cf-ray", "cf-mitigated", "retry-after"];
const VALID_TOKEN = /^[A-Za-z0-9_-]{32,256}$/;

function failure(code, status) {
  return Response.json({ ok: false, error: code }, { status, headers: {
    "x-corum-gd-relay": "v1", "x-corum-gd-relay-error": code, "cache-control": "no-store",
  } });
}
async function authorized(request, token) {
  const submitted = request.headers.get("authorization") ?? "";
  if (!submitted.startsWith("Bearer ") || !VALID_TOKEN.test(submitted.slice(7))) return false;
  const digest = async value => new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(value)));
  const [a, b] = await Promise.all([digest(submitted.slice(7)), digest(token)]);
  let difference = 0;
  for (let i = 0; i < a.length; i++) difference |= a[i] ^ b[i];
  return difference === 0;
}
async function boundedBytes(body, limit) {
  if (!body) return new Uint8Array();
  const reader = body.getReader(), parts = []; let size = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read(); if (done) break;
      size += value.byteLength; if (size > limit) throw new Error("size");
      parts.push(value);
    }
  } finally { await reader.cancel().catch(() => {}); }
  const bytes = new Uint8Array(size); let offset = 0;
  for (const part of parts) { bytes.set(part, offset); offset += part.byteLength; }
  return bytes;
}
function validateForm(path, text) {
  const fields = new URLSearchParams(text), seen = new Set();
  const extra = path === SEARCH ? ["type", "str", "star", "len", "page", "diff", "demonFilter"] : ["levelID"];
  const allowed = new Set(["gameVersion", "binaryVersion", "secret", ...extra]);
  for (const [key] of fields) { if (!allowed.has(key) || seen.has(key)) return null; seen.add(key); }
  if (fields.get("gameVersion") !== "22" || fields.get("binaryVersion") !== "45" || fields.get("secret") !== "Wmfd2893gb7") return null;
  if (path === SEARCH) {
    if (!["2","4","11"].includes(fields.get("type")) || fields.get("str") !== "" || fields.get("star") !== "1" || !["0,1,2,3","0,1,2,3,4"].includes(fields.get("len")) ||
      !/^(?:0|[1-9]\d{0,5})$/.test(fields.get("page") ?? "") || Number(fields.get("page")) > 199999) return null;
    const diff = fields.get("diff");
    if (diff === "-2" ? !/^[1-5]$/.test(fields.get("demonFilter") ?? "") : !/^[345]$/.test(diff ?? "") || fields.has("demonFilter")) return null;
  } else {
    const id = fields.get("levelID") ?? "";
    if (!/^[1-9]\d{7,15}$/.test(id) || !Number.isSafeInteger(Number(id)) || Number(id) < 40_000_000) return null;
  }
  // Re-encode the validated form rather than forwarding caller-controlled bytes.
  return fields.toString();
}
function limitedStream(body, maximum) {
  const reader = body.getReader(); let size = 0;
  return new ReadableStream({
    async pull(controller) {
      try {
        const { value, done } = await reader.read();
        if (done) { controller.close(); return; }
        size += value.byteLength;
        if (size > maximum) throw new Error("GD relay response exceeded size budget");
        controller.enqueue(value);
      } catch (error) { await reader.cancel().catch(() => {}); controller.error(error); }
    },
    cancel(reason) { return reader.cancel(reason); },
  });
}

export function createGdRelayWorker(upstreamFetch = fetch) {
  return { async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "GET" && url.pathname === "/health" && !url.search)
      return Response.json({ ok: true, service: "corum-ranked-gd-relay", protocol: "v1", configured: VALID_TOKEN.test(env.GD_RELAY_TOKEN ?? "") },
        { headers: { "cache-control": "no-store" } });
    if (![SEARCH, DOWNLOAD].includes(url.pathname) || url.search) return failure("INVALID_REQUEST", 404);
    if (request.method !== "POST") return failure("INVALID_REQUEST", 405);
    if (!VALID_TOKEN.test(env.GD_RELAY_TOKEN ?? "")) return failure("NOT_CONFIGURED", 503);
    if (!await authorized(request, env.GD_RELAY_TOKEN)) return failure("UNAUTHORIZED", 401);
    if ((request.headers.get("content-type") ?? "").split(";")[0].trim().toLowerCase() !== "application/x-www-form-urlencoded")
      return failure("INVALID_REQUEST", 400);
    let bytes, text;
    try { bytes = await boundedBytes(request.body, 4096); }
    catch { return failure("REQUEST_TOO_LARGE", 413); }
    try { text = new TextDecoder("utf-8", { fatal: true }).decode(bytes); }
    catch { return failure("INVALID_REQUEST", 400); }
    const form = validateForm(url.pathname, text);
    if (form === null) return failure("INVALID_REQUEST", 400);
    let upstream;
    const signal = AbortSignal.timeout(8_000);
    try {
      upstream = await upstreamFetch(`${GD_ORIGIN}${url.pathname.slice(1)}`, { method: "POST", redirect: "manual",
        headers: { "content-type": "application/x-www-form-urlencoded", accept: "*/*", "user-agent": "" },
        body: form, signal });
    } catch { return failure(signal.aborted ? "UPSTREAM_TIMEOUT" : "UPSTREAM_UNAVAILABLE", 502); }
    if (upstream.status >= 300 && upstream.status < 400) {
      await upstream.body?.cancel().catch(() => {}); return failure("UPSTREAM_REDIRECT", 502);
    }
    if (!upstream.body) return failure("UPSTREAM_UNAVAILABLE", 502);
    const maximum = url.pathname === SEARCH ? 2 * 1024 * 1024 : 24 * 1024 * 1024;
    const length = upstream.headers.get("content-length");
    if (length && /^\d+$/.test(length) && Number(length) > maximum) {
      await upstream.body.cancel().catch(() => {}); return failure("RESPONSE_TOO_LARGE", 502);
    }
    const headers = new Headers({ "x-corum-gd-relay": "v1", "x-corum-gd-upstream-status": String(upstream.status),
      "cache-control": "no-store", "content-type": upstream.headers.get("content-type") ?? "text/plain" });
    for (const name of UPSTREAM_HEADERS) {
      const value = upstream.headers.get(name); if (value !== null) headers.set(`x-corum-gd-upstream-${name}`, value);
    }
    return new Response(limitedStream(upstream.body, maximum), { status: upstream.status, headers });
  } };
}
export default createGdRelayWorker();
