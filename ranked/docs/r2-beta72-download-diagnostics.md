# R2 package delivery diagnostics (beta.72)

The supplied beta.70 Debug URL uses the public `r2.dev` development hostname,
not a Cloudflare R2 Custom Domain. The 7,982,567-byte package already sends
`Cache-Control: public, max-age=31536000, immutable` and
`Content-Type: application/octet-stream`. No Cache Status/Age header was observed
through this cloud environment's proxy. This does not prove end-user CDN hits.

Two fresh measurements on 2026-10-15 from this machine:

| Sample | Connect | First response | Total | Transfer | SHA-256 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | .0011s | .9790s | 1.2147s | 6.57 MB/s | 10.69ms |
| 2 | .0009s | .4409s | .6930s | 11.52 MB/s | 10.02ms |

Both returned HTTP 200 with no redirect and identical hash
`1fb3c0712d5498187c4353cb43c7fdff606f2d92827d5c4b412e5380c8b83c14`.
These include the configured cloud proxy and are not phone/desktop measurements.
The reported ~300kbps was not reproduced. An identical beta.70 package is not
available on GitHub Releases, so a same-file GitHub comparison is unverified.
No bandwidth improvement percentage is claimed.

The Geode 5.8.2 WebResponse API buffers downloads. Its existing request path is
retained; there is no speculative parallel/Range implementation. Progress is
coalesced to at most one queued UI update per 100ms (plus completion). Saving the
buffered response moves to the existing verification worker rather than blocking
the main-thread completion callback. Size, ZIP signature, SHA-256, version,
channel, platform binary and rollback checks remain intact.

`[RankedModUpdate]` logs now report the origin, HTTP status, transferred bytes,
elapsed request time/MBps, first progress byte time, content length, CF cache/Age/
Ray, worker save/hash/verification time and installation time. The first-byte
figure is an application progress callback observation, not network TTFB or TLS
timing. The CLI probe records curl connection/first-response/total/redirect
metrics independently:

```sh
python3 corum-ranked-mod/tools/probe-mod-download.py --samples 2 \
  --output download-probe.json 'https://YOUR-PUBLIC-ORIGIN/ranked/CHANNEL-VERSION/FILE.geode'
```

## Operator setup still required

1. In the existing `corum-ranked-download` R2 bucket, Settings → Public Access →
   Custom Domains, connect a hostname from a Cloudflare-managed DNS zone you own.
   No particular domain is assumed or created by this change.
2. On that hostname, create a Cache Rule for `/ranked/*`: eligible for cache,
   respect the existing one-year origin Cache-Control. If overriding Edge TTL,
   use one year only for these immutable version paths. Do not apply this rule
   to mutable application APIs.
3. Fetch an existing `.geode` through the new hostname twice from the same
   network. Verify identical bytes/hash/Content-Length and inspect
   `CF-Cache-Status`/`Age`; a repeated request should be eligible for HIT. Do not
   claim caching until measured on the real Custom Domain.
4. Keep `RANKED_DOWNLOAD_BASE_URL` pointing at the current descriptor origin.
   Set `RANKED_DOWNLOAD_CDN_BASE_URL=https://YOUR-CUSTOM-DOMAIN` only after step 3.
   Server descriptor lookup/attestation remains anchored to the original base;
   both channel URLs use the same exact immutable version/filename under the
   Custom Domain. Hash/size/channel/version metadata is not rewritten.
5. Rollback is to unset only the CDN override; existing descriptors and bucket
   objects need no rewrite. Publish new beta.72 objects normally, without
   overwriting older immutable versions. No production setting was changed here.

Measure again using the actual slow client and its network. Device save/hash/
install times and GitHub/R2 same-file comparison are still outstanding.
