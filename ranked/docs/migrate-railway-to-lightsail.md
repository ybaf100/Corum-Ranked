# Corum-Ranked → AWS Lightsail Sydney 4 GB (migration runbook)

**Status: infrastructure deployment files only; AWS instance NOT created and production NOT cut over.** Do not merge or dispatch before provisioning an AWS account and a secure Neon staging environment.

## Server choice / budget

- AWS Region: **Asia Pacific (Sydney) — ap-southeast-2**, the selected Region of the user's new AWS experience project. New AWS experience projects restrict Regional resource creation to their assigned Region.
- Lightsail: **Linux/Unix with public IPv4, 4 GiB RAM, 2 vCPU, 80 GB SSD, $24 USD/month standard plan**. The generic plan shows 4 TB data transfer, but **Sydney has only half: 2 TB/month**. Confirm the final bundle and project restrictions in the console.
- Image: Ubuntu 24.04 LTS **x86_64**.
- Two full months = **$48 in base instance fees**, excluding taxes, overages and other AWS resources.
- The AWS new-user $100 credit is **account-dependent and must be shown in Billing before assuming the server is covered**.
- After use: delete the instance and inspect static IP, snapshots and other paid resources. **Stopping an instance does not stop Lightsail instance charges.**

## Architecture

Geode client → HTTPS (Caddy on Lightsail static IP + DNS hostname) → Docker on 127.0.0.1:3000 → Neon PostgreSQL.

Cloudflare R2 hosts mod releases. Cloudflare Worker is optional GD relay. Neither requires migrating to AWS.

GitHub Actions builds the Node 22 Docker image; SSH sends a gzip image archive to Lightsail, and a root-owned, restricted deployment script switches containers and rolls back if the new /health endpoint fails.

## 1. Create Lightsail instance (AWS console)

1. Visit https://lightsail.aws.amazon.com/ and choose **Sydney (ap-southeast-2)**. In a new AWS experience project, check the project's assigned Region in AWS Settings first.
2. Create a **Linux/Unix → Ubuntu 24.04 LTS** instance. Select the **4 GB / 2 vCPU / 80 GB / $24** IPv4-capable bundle; in Sydney the included transfer allowance is **2 TB**, not 4 TB.
3. Set name `corum-ranked-sydney`. Confirm displayed monthly rate, any AWS credits, and the intended AWS project before clicking create.
4. Attach a **static public IP** in Lightsail Networking. Point your chosen DNS A record to it (example `api.yourdomain.com`).
5. Lightsail firewall: allow **TCP 443 and 80 from the internet**, **TCP 22 from your administrative IP only** wherever feasible. **Do not open 3000**. If GitHub-hosted runners need SSH access, allow a *documented, controlled* CI SSH path instead of permanently opening port 22 to the whole internet; GitHub Actions IP addresses can vary. For a brief deployment window, a temporary controlled exception may be needed.
6. Set a budget and usage alert in AWS Billing. Billing budgets and snapshots might add cost; review before enabling.

AWS Free Tier / Lightsail docs: https://aws.amazon.com/free/compute/lightsail/ and https://aws.amazon.com/lightsail/pricing/.

## 2. Install server prerequisites

In Lightsail's browser SSH terminal, obtain a trusted checkout/copy of this repository's **merged** `main` with the scripts in `ranked/deploy`. Then run:

```bash
cd Corum-Ranked
sudo bash ranked/deploy/lightsail-bootstrap.sh
```

This installs Docker Engine from the Ubuntu package repository and Caddy, creates `corum-deploy`, installs `/usr/local/sbin/corum-ranked-activate`, and creates a root-owned environment-file directory. It does not start the app or edit production DB.

If cloning the repository on the server is undesirable, copy the two scripts from a trusted local checkout to Lightsail over an authenticated SSH channel and run them locally on Lightsail.

## 3. GitHub Actions SSH credentials

1. Generate a **dedicated** Ed25519 SSH key pair for deployment, preferably on your own trusted device; **do not send the private key in chat**.
2. Add its **PUBLIC** key to `/home/corum-deploy/.ssh/authorized_keys` on Lightsail, setting the directory to 0700 and file to 0600 and owner `corum-deploy:corum-deploy`.
3. Pin and verify the server SSH host public key against the Lightsail server itself, **not only an unverified ssh-keyscan** output.
4. In GitHub repository **Settings → Environments**, create `lightsail-production` and configure optional required reviewer approval.
5. Configure environment secrets:
   - `LIGHTSAIL_SSH_PRIVATE_KEY` = private deploy key
   - `LIGHTSAIL_SSH_KNOWN_HOSTS` = pinned OpenSSH known_hosts entry for the static IP
6. Configure environment variable:
   - `LIGHTSAIL_HOST` = Lightsail static IPv4 address

**Network reminder:** GitHub-hosted runner SSH needs reachability to port 22. If using tight allowlists and runners with changing IP ranges, use a self-hosted runner or a secure pull-based mechanism instead. Never add an unbounded SSH bypass in the app for convenience.

## 4. Application secrets — do NOT copy compromised Railway secrets

Create/edit `/etc/corum-ranked/server.env` using `sudoedit`; permissions root:root / 0600. Refer to `ranked/deploy/server.env.example`. Use **a Neon staging branch** for first application boot; never boot an unvalidated second production writer against the production Neon branch.

At minimum:
- `NODE_ENV=production`; `PORT=3000`
- `DATABASE_URL`: staging Neon connection string initially; NEW production password at cutover
- `RANKED_CONFIG_URL`: existing HTTPS Apps Script configuration endpoint
- `RANKED_CONFIG_REFRESH_MS`, `RANKED_CONFIG_FETCH_TIMEOUT_MS` (required)
- `RANKED_SESSION_TOKEN_SECRET`: new 32+ random-byte value; rotating invalidates previous session hash/match token derivation
- `RANKED_DOWNLOAD_BASE_URL`: existing Cloudflare R2 public URL, if used
- `SURVIVAL_GD_RELAY_ORIGIN` and `SURVIVAL_GD_RELAY_TOKEN`: both set together if using relay; rotate the matching Cloudflare Worker `GD_RELAY_TOKEN`
- `DISCORD_WEBHOOK_URL`: new webhook if configured; fill `DISCORD_OUTBOX_*` policy fields when enabled
- `ENABLE_DEBUG_BOT_MATCH=false`; `ENABLE_DEBUG_SPECTATE=false` in production

**No secret values in GitHub repo, Actions workflow code, chat messages, or deployment logs.** The GitHub Actions SSH key is unrelated to the app session signing secret.

## 5. HTTPS reverse proxy

Copy `ranked/deploy/Caddyfile.example` to `/etc/caddy/Caddyfile`, replace `api.example.com` with YOUR DNS hostname, and validate/reload:

```bash
sudo caddy validate --config /etc/caddy/Caddyfile
sudo systemctl reload caddy
```

Caddy will request/renew TLS certificates when DNS and ports 80/443 work. The API container only publishes `127.0.0.1:3000`, never public port 3000. An HTTPS hostname is necessary before updating Geode clients; a static IP by itself is not a substitute for the existing HTTPS endpoint.

## 6. Deploy server with GitHub Actions

After the runbook files are reviewed/merged into `main`, run the manual workflow **Deploy Corum Ranked to Lightsail (manual)**. This workflow is deliberately **not** triggered by ordinary pushes and cannot run from a non-main ref.

GitHub runner builds the Node 22 image, uploads it via pinned SSH, and invokes a restricted root-owned deployment script. The script:
- validates the commit SHA and required environment keys
- imports the immutable Docker image
- retains the previous stopped container for rollback
- starts the new read-only container with capped resources
- checks `http://127.0.0.1:3000/health`
- restores the previous container if the health check fails

After startup on the Lightsail host, independently verify:
```bash
curl -fsS http://127.0.0.1:3000/health
curl -fsS http://127.0.0.1:3000/ready
curl -fsS https://api.yourdomain.com/health
curl -fsS https://api.yourdomain.com/ready
```
Inspect `/ready` fields (especially ranked readiness); an HTTP 200 response alone does not prove matchmaking, migrations or config succeeded.

## 7. Production cutover, preserving the Neon DB

**Do not run Railway and Lightsail simultaneously against the same live Neon DB.** Cron / background workers, matchmaking, session ownership and settlements can conflict.

1. Back up the Neon production branch, verify a restoration path and inspect any suspicious changes after the October 2026 Railway incident.
2. Test Lightsail against the Neon **staging** branch, including authentication, matchmaking, Solo Ranked, Survival, achievements, LP/MMR, spectator flows, R2 downloads and GD relay.
3. Announce maintenance; prevent new match admission and allow/resolve in-flight matches.
4. Stop Railway production only after staging passes (unless emergency credential revocation requires earlier isolation).
5. Rotate production Neon DB credentials and session key; ensure Railway's old credentials are invalid.
6. Update Lightsail's root-owned environment file to the new production credentials and restart/activate exactly one authoritative production server.
7. Verify full readiness and completed games using test clients.
8. **Geode mod hardcodes the Railway URL.** Update these code locations after the new HTTPS hostname is known, rebuild, and release the compatible native package through R2:
   - `corum-ranked-mod/src/RankedRuntime.cpp`
   - `corum-ranked-mod/src/SurvivalRuntime.cpp`
   - `corum-ranked-mod/src/SpectatorLiveRuntime.cpp`
   - `corum-ranked-mod/src/debug/DebugAdminPopup.cpp`
   - `corum-ranked-mod/src/debug/DebugSpectateRuntime.cpp`
   - `corum-ranked-mod/tests/ProductionServerEndpointTest.mjs`
   - deployment instructions (`AGENTS.md` and relevant docs)
9. Synchronize server exact-version gates, `.geode` manifests and R2 release pipeline. Previously installed binaries do **not** automatically move from the old URL. A legacy-domain forwarding strategy may be necessary for seamless upgrade, if still available.
10. Monitor live requests, LP/MMR ledger and migrations before deleting the old Railway service.

## 8. After ~2 months

- Export or verify any data that is not already in Neon/R2.
- In AWS Lightsail, **delete the VM** rather than merely stopping it.
- Release unattached static IP and remove unused snapshots/disks. Verify the Billing dashboard for any residual charges.
- If using a Paid plan, credits may expire/run out and charges may accrue. Confirm the credit balance before and during use.

## Known limitations of this preparatory PR

- No AWS account is connected to ChatGPT. No instance, SSH credentials, static IP, DNS or real HTTPS hostname has been provisioned.
- GitHub Actions / Docker build and a real Lightsail deploy have not been run. Review and test after merging.
- The existing Railway URL remains in the active Geode client until a working Lightsail HTTPS domain is assigned and a coordinated client/server release is prepared.
- Docker deployment health tests do not replace integrated Ranked/Survival tests.
- GitHub-hosted runner egress IPs are dynamic; the required SSH firewall strategy must be chosen by the operator.
