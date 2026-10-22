#!/usr/bin/env bash
# Run once on Ubuntu 24.04 Lightsail VM, from a trusted local checkout.
set -Eeuo pipefail
if [[ ${EUID} -ne 0 ]]; then echo "Run using sudo" >&2; exit 1; fi
if [[ ! -f /etc/os-release ]]; then echo "Ubuntu required" >&2; exit 1; fi
. /etc/os-release
if [[ "${ID}" != "ubuntu" ]]; then echo "Expected Ubuntu; found ${ID}" >&2; exit 1; fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y docker.io caddy curl ca-certificates
systemctl enable --now docker caddy

if ! id -u corum-deploy >/dev/null 2>&1; then
  useradd --create-home --shell /bin/bash corum-deploy
fi
install -d -o corum-deploy -g corum-deploy -m 0750 /var/lib/corum-ranked/incoming
install -d -o root -g root -m 0700 /etc/corum-ranked
if [[ ! -e /etc/corum-ranked/server.env ]]; then
  install -o root -g root -m 0600 /dev/null /etc/corum-ranked/server.env
fi

SOURCE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/lightsail-activate.sh"
install -o root -g root -m 0755 "$SOURCE" /usr/local/sbin/corum-ranked-activate
echo 'corum-deploy ALL=(root) NOPASSWD: /usr/local/sbin/corum-ranked-activate *' > /etc/sudoers.d/corum-ranked-deploy
chmod 0440 /etc/sudoers.d/corum-ranked-deploy
visudo -cf /etc/sudoers.d/corum-ranked-deploy

echo 'Bootstrap complete. Next:'
echo '  1. Add the GitHub Actions deployment PUBLIC key to /home/corum-deploy/.ssh/authorized_keys.'
echo '  2. Edit /etc/corum-ranked/server.env with NEW application credentials.'
echo '  3. Point your DNS to the Lightsail static IP, then configure Caddy HTTPS.'
echo '  4. Configure GitHub Actions environment secrets and dispatch the manual deployment.'
