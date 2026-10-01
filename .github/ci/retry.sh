#!/usr/bin/env bash
# Runs a command up to three times, retrying ONLY when its output shows a
# network failure (download, DNS, TLS, HTTP 5xx). A compile error or any other
# failure stops at once, so a genuine break never burns runner time twice.
#
#   bash .github/ci/retry.sh <command> [args...]
set -uo pipefail

network_pattern='Failed to download|failed to download|Could not resolve host|Could not resolve hostname|Temporary failure in name resolution|Connection timed out|Connection reset|Connection refused|Operation timed out|timed out after|SSL_ERROR|SSL connect error|TLS handshake|gnutls_handshake|unexpected EOF|early EOF|RPC failed|The requested URL returned error: 5|HTTP error 5|status code 5[0-9][0-9]|curl: \([0-9]+\)|Hash Sum mismatch|Unable to fetch some archives|Failed to fetch|Could not connect to|error: downloading|Error: Download failed|ETIMEDOUT|ECONNRESET'

log="$(mktemp)"
trap 'rm -f "$log"' EXIT
delays=(0 20 60)
for attempt in 1 2 3; do
    if ((attempt > 1)); then
        echo "::warning::Network failure detected; retry $attempt/3 in ${delays[$((attempt - 1))]}s: $*"
        sleep "${delays[$((attempt - 1))]}"
    fi
    "$@" 2>&1 | tee "$log"
    status=${PIPESTATUS[0]}
    if ((status == 0)); then
        exit 0
    fi
    if ! grep -Eq "$network_pattern" "$log"; then
        echo "::error::Command failed (exit $status) without a network signature; not retrying."
        exit "$status"
    fi
done
echo "::error::Command still failing after 3 attempts: $*"
exit "$status"
