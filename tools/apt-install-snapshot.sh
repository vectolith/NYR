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

snapshot=${NYR_APT_SNAPSHOT:-}
marker=/var/lib/apt/lists/.nyr-snapshot
cached_indexes=0
if [ "${NYR_APT_USE_CACHED_INDEXES:-0}" = 1 ] &&
   [ -n "$snapshot" ] && [ -r "$marker" ] &&
   [ "$(cat "$marker")" = "$snapshot" ] &&
   package_index_complete "$@"; then
    cached_indexes=1
else
    rm -rf /var/lib/apt/lists/*
fi

attempt=1
# Reuse indexes only when the immutable snapshot identity matches. On a
# cache miss, preserve successfully fetched indexes across bounded retries.
max_attempts=4
while [ "$attempt" -le "$max_attempts" ]; do
    indexes_ready=0
    if [ "$cached_indexes" -eq 1 ]; then
        indexes_ready=1
        cached_indexes=0
    elif apt-get -o Acquire::Retries=1 \
                 -o Acquire::http::Timeout=20 \
                 -o Acquire::https::Timeout=20 update &&
         package_index_complete "$@"; then
        indexes_ready=1
        if [ -n "$snapshot" ]; then
            printf '%s\n' "$snapshot" > "$marker"
        fi
    fi

    if [ "$indexes_ready" -eq 1 ] &&
       DEBIAN_FRONTEND=noninteractive apt-get \
           -o Acquire::Retries=1 \
           -o Acquire::http::Timeout=20 \
           -o Acquire::https::Timeout=20 \
           install -y --no-install-recommends "$@"; then
        exit 0
    fi

    if [ "$attempt" -eq "$max_attempts" ]; then
        echo "APT snapshot remained incomplete after $max_attempts attempts" >&2
        exit 1
    fi
    echo "APT snapshot attempt $attempt/$max_attempts failed; retrying" >&2
    delay=$((attempt * 10))
    if [ "$delay" -gt 120 ]; then delay=120; fi
    echo "Waiting ${delay}s before the next snapshot attempt" >&2
    sleep "$delay"
    attempt=$((attempt + 1))
done
exit 1
