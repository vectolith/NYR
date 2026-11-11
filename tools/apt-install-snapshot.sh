#!/bin/sh
set -eu

if [ "$#" -eq 0 ]; then
    echo "usage: apt-install-snapshot PACKAGE..." >&2
    exit 2
fi

package_index_complete() {
    for package in "$@"; do
        candidate=$(apt-cache policy "$package" 2>/dev/null |
            awk '/Candidate:/ { print $2; exit }')
        if [ -z "$candidate" ] || [ "$candidate" = "(none)" ]; then
            return 1
        fi
    done
    return 0
}

attempt=1
# Snapshot-service 5xx responses can persist for several minutes. Keep the
# immutable snapshot and retry with bounded backoff within the CI job timeout.
max_attempts=8
while [ "$attempt" -le "$max_attempts" ]; do
    rm -rf /var/lib/apt/lists/*
    if apt-get -o Acquire::Retries=2 \
               -o Acquire::http::Timeout=30 \
               -o Acquire::https::Timeout=30 update &&
       package_index_complete "$@" &&
       DEBIAN_FRONTEND=noninteractive apt-get \
           -o Acquire::Retries=2 \
           -o Acquire::http::Timeout=30 \
           -o Acquire::https::Timeout=30 \
           install -y --no-install-recommends "$@"; then
        exit 0
    fi

    if [ "$attempt" -eq "$max_attempts" ]; then
        echo "APT snapshot remained incomplete after $max_attempts attempts" >&2
        exit 1
    fi
    echo "APT snapshot attempt $attempt/$max_attempts failed; retrying" >&2
    delay=$((attempt * 15))
    if [ "$delay" -gt 120 ]; then
        delay=120
    fi
    echo "Waiting ${delay}s before the next snapshot attempt" >&2
    sleep "$delay"
    attempt=$((attempt + 1))
done

exit 1
