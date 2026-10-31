#!/usr/bin/env bash
set -euo pipefail
# Tested actionlint v1.7.7, checked against its upstream release checksums.
# Never execute a moving latest download or an unchecked cached executable.
workflow_lint_tmp="$(mktemp -d)"
trap 'rm -rf "$workflow_lint_tmp"' EXIT
curl --fail --silent --show-error --location --retry 3 \
  https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz \
  -o "$workflow_lint_tmp/actionlint.tar.gz"
printf '%s  %s\n' 023070a287cd8cccd71515fedc843f1985bf96c436b7effaecce67290e7e0757 \
  "$workflow_lint_tmp/actionlint.tar.gz" | sha256sum --check --status
tar -xzf "$workflow_lint_tmp/actionlint.tar.gz" -C "$workflow_lint_tmp" actionlint
"$workflow_lint_tmp/actionlint" -shellcheck='' -pyflakes=''
