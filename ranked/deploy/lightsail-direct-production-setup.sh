#!/usr/bin/env bash
# Direct production configuration. Does not start the app or modify Neon/Railway.
# Usage: sudo bash lightsail-direct-production-setup.sh <STATIC_PUBLIC_IPV4>
set -Eeuo pipefail
umask 077
if [[ "$EUID" -ne 0 || "$#" -ne 1 || ! "$1" =~ ^([0-9]{1,3}\.){3}[0-9]{1,3}$ ]]; then
  echo "Usage: sudo bash $0 <Lightsail static IPv4>" >&2; exit 2
fi
IP="$1"
HOST="api.$(printf '%s' "$IP" | tr . -).sslip.io"
if [[ ! -d /etc/corum-ranked ]] || ! command -v caddy >/dev/null || ! command -v openssl >/dev/null; then
  echo "First run the existing Lightsail bootstrap (Docker, Caddy and /etc/corum-ranked)." >&2; exit 1
fi
echo "Production API hostname: https://$HOST"
echo "Provide the actual production connection string, NOT a staging branch."
read -r -s -p "Neon production DATABASE_URL (hidden): " DB; echo
if [[ "$DB" != postgres://* && "$DB" != postgresql://* ]] || [[ "$DB" != *ep-raspy-dream-az49jdlg* ]]; then
  echo "Wrong DATABASE_URL. It must be the verified Neon production endpoint." >&2; exit 1
fi
read -r -p "Existing Apps Script /exec?action=ranked_config URL: " CONFIG
if [[ "$CONFIG" != https://script.google.com/macros/s/*/exec* || "$CONFIG" != *action=ranked_config* ]]; then
  echo "Invalid RANKED_CONFIG_URL" >&2; exit 1
fi
read -r -p "Existing R2 HTTPS download base URL (Enter to omit): " R2
if [[ -n "$R2" && "$R2" != https://* ]]; then echo "R2 URL must use HTTPS" >&2; exit 1; fi
read -r -p "GD relay HTTPS origin (Enter to omit): " RELAY_ORIGIN
RELAY_TOKEN=""
if [[ -n "$RELAY_ORIGIN" ]]; then
  if [[ "$RELAY_ORIGIN" != https://* ]]; then echo "Relay must use HTTPS" >&2; exit 1; fi
  read -r -s -p "Matching GD relay token (hidden): " RELAY_TOKEN; echo
  if [[ -z "$RELAY_TOKEN" ]]; then echo "Relay token required" >&2; exit 1; fi
fi
ENV_TMP="$(mktemp /etc/corum-ranked/.server.env.XXXXXX)"
CADDY_TMP="$(mktemp /etc/caddy/.Caddyfile.XXXXXX)"
trap 'rm -f "$ENV_TMP" "$CADDY_TMP"' EXIT
{
  printf 'NODE_ENV=production\nPORT=3000\n'
  printf 'DATABASE_URL=%s\n' "$DB"
  printf 'RANKED_CONFIG_URL=%s\n' "$CONFIG"
  printf 'RANKED_CONFIG_REFRESH_MS=60000\nRANKED_CONFIG_FETCH_TIMEOUT_MS=10000\nRANKED_CSMP_FETCH_TIMEOUT_MS=30000\n'
  printf 'RANKED_SESSION_TOKEN_SECRET=%s\n' "$(openssl rand -hex 32)"
  printf 'ENABLE_DEBUG_BOT_MATCH=false\nENABLE_DEBUG_SPECTATE=false\n'
  if [[ -n "$R2" ]]; then printf 'RANKED_DOWNLOAD_BASE_URL=%s\n' "$R2"; fi
  if [[ -n "$RELAY_ORIGIN" ]]; then
    printf 'SURVIVAL_GD_RELAY_ORIGIN=%s\nSURVIVAL_GD_RELAY_TOKEN=%s\n' "$RELAY_ORIGIN" "$RELAY_TOKEN"
  fi
} > "$ENV_TMP"
chmod 0600 "$ENV_TMP"
chown root:root "$ENV_TMP"
printf '%s {\n    encode zstd gzip\n    reverse_proxy 127.0.0.1:3000\n}\n' "$HOST" > "$CADDY_TMP"
caddy validate --config "$CADDY_TMP" --adapter caddyfile
if [[ -f /etc/caddy/Caddyfile ]]; then cp -p /etc/caddy/Caddyfile "/etc/caddy/Caddyfile.pre-corum-$(date +%Y%m%d%H%M%S)"; fi
if [[ -s /etc/corum-ranked/server.env ]]; then cp -p /etc/corum-ranked/server.env "/etc/corum-ranked/server.env.pre-corum-$(date +%Y%m%d%H%M%S)"; fi
mv "$CADDY_TMP" /etc/caddy/Caddyfile
systemctl enable --now caddy
systemctl reload caddy
mv "$ENV_TMP" /etc/corum-ranked/server.env
trap - EXIT
unset DB CONFIG RELAY_TOKEN
echo "Configured https://$HOST (Caddy + production env). Docker app NOT deployed."
echo "Ensure Lightsail inbound TCP 80 and 443, stop old Railway writers before the production deploy."
