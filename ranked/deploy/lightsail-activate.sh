#!/usr/bin/env bash
# Restricted sudo entry point invoked by the Lightsail GitHub Actions deploy workflow.
set -Eeuo pipefail
if [[ "${EUID}" -ne 0 || "$#" -ne 1 || ! "$1" =~ ^[0-9a-f]{40}$ ]]; then
  echo "Usage (root): corum-ranked-activate <40-digit git SHA>" >&2
  exit 2
fi
SHA="$1"
ARTIFACT="/var/lib/corum-ranked/incoming/${SHA}.tar.gz"
ENV_FILE="/etc/corum-ranked/server.env"
IMAGE="corum-ranked:${SHA}"
APP="corum-ranked"
PREVIOUS="corum-ranked-previous"

[[ -f "$ARTIFACT" && ! -L "$ARTIFACT" ]] || { echo "Missing deployment artifact" >&2; exit 1; }
[[ -s "$ENV_FILE" ]] || { echo "Missing / empty application environment" >&2; exit 1; }
for name in DATABASE_URL RANKED_SESSION_TOKEN_SECRET RANKED_CONFIG_URL RANKED_CONFIG_REFRESH_MS RANKED_CONFIG_FETCH_TIMEOUT_MS; do
  grep -q "^${name}=" "$ENV_FILE" || { echo "Required variable is missing: ${name}" >&2; exit 1; }
done
if docker container inspect "$PREVIOUS" >/dev/null 2>&1; then
  echo "Previous recovery container still exists; resolve before retrying" >&2
  exit 1
fi

gzip -dc "$ARTIFACT" | docker load >/dev/null
docker image inspect "$IMAGE" >/dev/null

HAS_PREVIOUS=0
rollback() {
  echo "Deployment failed; attempting previous container rollback" >&2
  docker rm -f "$APP" >/dev/null 2>&1 || true
  if [[ "$HAS_PREVIOUS" == 1 ]]; then
    docker rename "$PREVIOUS" "$APP" || true
    docker start "$APP" >/dev/null || true
  fi
}
trap rollback ERR

if docker container inspect "$APP" >/dev/null 2>&1; then
  docker stop --time 45 "$APP" >/dev/null
  docker rename "$APP" "$PREVIOUS"
  HAS_PREVIOUS=1
fi

docker run -d \
  --name "$APP" \
  --restart unless-stopped \
  --read-only \
  --tmpfs /tmp:rw,nosuid,size=128m \
  --security-opt no-new-privileges \
  --cap-drop ALL \
  --pids-limit 256 \
  --memory 4g \
  --env-file "$ENV_FILE" \
  -p 127.0.0.1:3000:3000 \
  "$IMAGE" >/dev/null

ready=0
for i in $(seq 1 45); do
  if curl --fail --silent --show-error --max-time 3 http://127.0.0.1:3000/health >/dev/null 2>&1; then
    ready=1
    break
  fi
  sleep 2
done
if [[ "$ready" -ne 1 ]]; then
  echo "Health check failed" >&2
  false
fi

trap - ERR
if [[ "$HAS_PREVIOUS" == 1 ]]; then docker rm "$PREVIOUS" >/dev/null; fi
rm -f "$ARTIFACT"
echo "Deployment successful: ${SHA}"
