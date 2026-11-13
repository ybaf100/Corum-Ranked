#!/usr/bin/env bash
# One-time root setup on the Singapore Lightsail host: serve the API behind
# Cloudflare at api.hwanhee1.com (and keep the legacy sslip.io name for old
# clients), pass the real client address to the server and bound slow or
# oversized requests at the edge.
#
#   sudo bash lightsail-cloudflare-edge.sh
#
# Re-running it is safe: it validates the new Caddyfile before replacing the
# current one, keeps a timestamped backup and restores it if Caddy refuses to
# reload.
set -Eeuo pipefail
[[ "${EUID}" -eq 0 ]] || { echo "Run as root (sudo)." >&2; exit 2; }
command -v caddy >/dev/null || { echo "Caddy is not installed." >&2; exit 1; }

API_HOST="${API_HOST:-api.hwanhee1.com}"
LEGACY_HOST="${LEGACY_HOST:-api.52-74-37-130.sslip.io}"

# Published at https://www.cloudflare.com/ips/ (fallback if the live list cannot be read).
FALLBACK_RANGES="173.245.48.0/20 103.21.244.0/22 103.22.200.0/22 103.31.4.0/22 141.101.64.0/18 108.162.192.0/18
190.93.240.0/20 188.114.96.0/20 197.234.240.0/22 198.41.128.0/17 162.158.0.0/15 104.16.0.0/13 104.24.0.0/14
172.64.0.0/13 131.0.72.0/22 2400:cb00::/32 2606:4700::/32 2803:f800::/32 2405:b500::/32 2405:8100::/32
2a06:98c0::/29 2c0f:f248::/32"
live="$( { curl -fsS --max-time 15 https://www.cloudflare.com/ips-v4; echo; curl -fsS --max-time 15 https://www.cloudflare.com/ips-v6; } 2>/dev/null || true)"
ranges="$(printf '%s\n' "$live" | grep -E '^[0-9a-fA-F:.]+/[0-9]{1,3}$' | tr '\n' ' ' || true)"
if [[ "$(wc -w <<<"$ranges")" -lt 10 ]]; then
  echo "Using the bundled Cloudflare ranges (live list unavailable)."
  ranges="$(tr '\n' ' ' <<<"$FALLBACK_RANGES")"
fi
ranges="$(xargs <<<"$ranges")"

CADDY_TMP="$(mktemp /etc/caddy/.Caddyfile.XXXXXX)"
trap 'rm -f "$CADDY_TMP"' EXIT
cat > "$CADDY_TMP" <<CADDY
{
	servers {
		timeouts {
			read_header 10s
			read_body 30s
			write 60s
			idle 2m
		}
		max_header_size 32KB
	}
}

${API_HOST}, ${LEGACY_HOST} {
	encode zstd gzip
	request_body {
		max_size 16MB
	}
	# The server trusts this header only from the host; it is always overwritten
	# here so a client cannot choose its own address.
	@cloudflare remote_ip ${ranges}
	@direct not remote_ip ${ranges}
	request_header @direct X-Corum-Client-IP {http.request.remote.host}
	request_header @cloudflare X-Corum-Client-IP {http.request.header.CF-Connecting-IP}
	reverse_proxy 127.0.0.1:3000
}
CADDY
chown root:root "$CADDY_TMP"
chmod 0644 "$CADDY_TMP"
caddy validate --config "$CADDY_TMP" --adapter caddyfile

backup=""
if [[ -f /etc/caddy/Caddyfile ]]; then
  backup="/etc/caddy/Caddyfile.pre-cloudflare-$(date +%Y%m%d%H%M%S)"
  cp -p /etc/caddy/Caddyfile "$backup"
fi
mv "$CADDY_TMP" /etc/caddy/Caddyfile
if ! systemctl reload caddy; then
  echo "Caddy refused the new configuration; restoring the previous one." >&2
  [[ -n "$backup" ]] && cp -p "$backup" /etc/caddy/Caddyfile && systemctl reload caddy
  exit 1
fi
echo "Caddy now serves ${API_HOST} and ${LEGACY_HOST}."
echo "Check: curl -fsS https://${API_HOST}/health"
