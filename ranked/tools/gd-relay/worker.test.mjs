import { test } from "node:test";
import assert from "node:assert/strict";
import { createGdRelayWorker } from "./worker.mjs";

const token = "fixture_" + "a".repeat(48), env = { GD_RELAY_TOKEN: token };
const search = { gameVersion: "22", binaryVersion: "45", secret: "Wmfd2893gb7", type: "4", str: "", star: "1", len: "0,1,2,3,4", page: "0", diff: "3" };
const download = { gameVersion: "22", binaryVersion: "45", secret: "Wmfd2893gb7", levelID: "40000000" };
function request(path = "/getGJLevels21.php", body = new URLSearchParams(search).toString(), headers = {}) {
  return new Request(`https://relay.example${path}`, { method: "POST", body,
    headers: { "content-type": "application/x-www-form-urlencoded", authorization: `Bearer ${token}`, ...headers } });
}
function noUpstream() { return createGdRelayWorker(() => { assert.fail("invalid requests must not reach GD"); }); }

test("health describes relay configuration without querying GD or exposing the key", async () => {
  const worker = noUpstream();
  for (const configured of [true, false]) {
    const r = await worker.fetch(new Request("https://relay.example/health"), configured ? env : {});
    assert.deepEqual(await r.json(), { ok: true, service: "corum-ranked-gd-relay", protocol: "v1", configured });
  }
});
test("missing configuration and invalid authorization fail before fetching GD", async () => {
  assert.equal((await noUpstream().fetch(request(), {})).status, 503);
  for (const auth of ["", "Basic bad", "Bearer bad", `Bearer ${"b".repeat(56)}`]) {
    const r = await noUpstream().fetch(request(undefined, undefined, { authorization: auth }), env);
    assert.equal(r.status, 401); assert.equal(r.headers.get("x-corum-gd-relay-error"), "UNAUTHORIZED");
  }
});
test("only exact public GD routes accept authenticated POST forms", async () => {
  for (const path of ["/", "/database/getGJLevels21.php", "/downloadGJLevel22.php?url=https://other.example", "/getGJLevels21.php/", "/deleteGJLevelUser20.php"])
    assert.equal((await noUpstream().fetch(request(path), env)).status, 404);
  assert.equal((await noUpstream().fetch(new Request("https://relay.example/getGJLevels21.php"), env)).status, 405);
  assert.equal((await noUpstream().fetch(request(undefined, undefined, { "content-type": "application/json" }), env)).status, 400);
});
test("rejects account fields, duplicate fields and out-of-scope search filters", async () => {
  const forms = [
    { ...search, url: "https://other.example" }, { ...search, accountID: "123" }, { ...search, type: "0" },
    { ...search, star: "0" }, { ...search, len: "5" }, { ...search, str: "chosen-map" },
    { ...search, page: "200000" }, { ...search, page: "-1" }, { ...search, diff: "2" },
    { ...search, diff: "-2" }, { ...search, diff: "-2", demonFilter: "6" }, { ...search, demonFilter: "1" },
    { ...search, secret: "wrong" }, { ...search, binaryVersion: "1" },
  ].map(form => new URLSearchParams(form).toString());
  forms.push(new URLSearchParams(search).toString() + "&diff=3");
  for (const form of forms) assert.equal((await noUpstream().fetch(request(undefined, form), env)).status, 400);
});
test("forwards all supported normal and Demon filters without forwarding caller credentials", async () => {
  for (const diff of ["3", "4", "5", "-2:1", "-2:2", "-2:3", "-2:4", "-2:5"]) {
    const [normal, demon] = diff.split(":");
    const body = new URLSearchParams({ ...search, page: "255", diff: normal, ...(demon ? { demonFilter: demon } : {}) }).toString();
    let called = false;
    const worker = createGdRelayWorker(async (url, init) => {
      called = true; assert.equal(url, "https://www.boomlings.com/database/getGJLevels21.php");
      assert.equal(init.body, body); assert.equal(init.method, "POST"); assert.equal(init.redirect, "manual");
      const h = new Headers(init.headers);
      assert.equal(h.get("authorization"), null); assert.equal(h.get("cookie"), null); assert.equal(h.get("user-agent"), "");
      assert.equal(h.get("x-forwarded-for"), null); assert.ok(init.signal instanceof AbortSignal);
      return new Response("GD-list", { headers: { "set-cookie": "private-upstream-cookie" } });
    });
    const r = await worker.fetch(request(undefined, body, { cookie: "private-client-cookie", "x-forwarded-for": "127.0.0.1" }), env);
    assert.ok(called); assert.equal(await r.text(), "GD-list"); assert.equal(r.headers.get("set-cookie"), null);
  }
});
test("download allows the exact 40-million boundary and rejects invalid IDs", async () => {
  const worker = createGdRelayWorker(async (url, init) => {
    assert.equal(url, "https://www.boomlings.com/database/downloadGJLevel22.php");
    assert.equal(new URLSearchParams(init.body).get("levelID"), "40000000"); return new Response("GD-level");
  });
  assert.equal((await worker.fetch(request("/downloadGJLevel22.php", new URLSearchParams(download).toString()), env)).status, 200);
  for (const id of ["39999999", "0", "4e7", "9007199254740992", "40000000&url=bad"]) {
    const r = await noUpstream().fetch(request("/downloadGJLevel22.php", new URLSearchParams({ ...download, levelID: id }).toString()), env);
    assert.equal(r.status, 400);
  }
});
test("limits request bytes before parsing or forwarding", async () => {
  const r = await noUpstream().fetch(request(undefined, "x".repeat(4097)), env);
  assert.equal(r.status, 413); assert.equal(r.headers.get("x-corum-gd-relay-error"), "REQUEST_TOO_LARGE");
});
test("rejects malformed UTF-8 separately from oversized requests", async () => {
  const r = await noUpstream().fetch(request(undefined, new Uint8Array([0xff, 0xfe])), env);
  assert.equal(r.status, 400); assert.equal(r.headers.get("x-corum-gd-relay-error"), "INVALID_REQUEST");
});
test("forwards GD denial status and original diagnostic headers without cookies", async () => {
  const worker = createGdRelayWorker(async () => new Response("error code: 1005", { status: 403,
    headers: { server: "cloudflare", "cf-ray": "upstream-SIN", "retry-after": "30", "content-type": "text/plain", "set-cookie": "secret" } }));
  const r = await worker.fetch(request(), env);
  assert.equal(r.status, 403); assert.equal(r.headers.get("x-corum-gd-upstream-status"), "403");
  assert.equal(r.headers.get("x-corum-gd-upstream-server"), "cloudflare");
  assert.equal(r.headers.get("x-corum-gd-upstream-cf-ray"), "upstream-SIN");
  assert.equal(r.headers.get("x-corum-gd-upstream-retry-after"), "30");
  assert.equal(await r.text(), "error code: 1005"); assert.equal(r.headers.get("set-cookie"), null);
});
test("network failures and redirects return sanitized relay errors", async () => {
  const broken = createGdRelayWorker(async () => { throw new Error(token); });
  const r = await broken.fetch(request(), env);
  assert.equal(r.status, 502); assert.equal(r.headers.get("x-corum-gd-relay-error"), "UPSTREAM_UNAVAILABLE");
  assert.ok(!(await r.text()).includes(token));
  const redirect = createGdRelayWorker(async () => new Response("redirect", { status: 302, headers: { location: "https://other.example" } }));
  assert.equal((await redirect.fetch(request(), env)).headers.get("x-corum-gd-relay-error"), "UPSTREAM_REDIRECT");
});
test("distinguishes timeout from an upstream network failure", async t => {
  t.mock.method(AbortSignal, "timeout", () => AbortSignal.abort());
  const worker = createGdRelayWorker(async () => { throw new Error("aborted"); });
  assert.equal((await worker.fetch(request(), env)).headers.get("x-corum-gd-relay-error"), "UPSTREAM_TIMEOUT");
});
test("rejects excessive declared response sizes before returning a data stream", async () => {
  for (const [path, form, maximum] of [["/getGJLevels21.php", search, 2 * 1024 * 1024], ["/downloadGJLevel22.php", download, 24 * 1024 * 1024]]) {
    const worker = createGdRelayWorker(async () => new Response("x", { headers: { "content-length": String(maximum + 1) } }));
    const r = await worker.fetch(request(path, new URLSearchParams(form).toString()), env);
    assert.equal(r.status, 502); assert.equal(r.headers.get("x-corum-gd-relay-error"), "RESPONSE_TOO_LARGE");
  }
});
test("caps unknown-length response streams and cancels the upstream on overflow", async () => {
  let cancelled = false;
  const worker = createGdRelayWorker(async () => new Response(new ReadableStream({ start(c) { c.enqueue(new Uint8Array(2 * 1024 * 1024 + 1)); }, cancel() { cancelled = true; } })));
  const r = await worker.fetch(request(), env);
  await assert.rejects(r.arrayBuffer(), /size budget/); assert.ok(cancelled);
});
