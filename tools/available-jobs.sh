#!/bin/sh
# Return a conservative parallel-job count bounded by CPU affinity and cgroup quota.
set -eu
cap=${1:-4}
case "$cap" in ''|*[!0-9]*) echo "usage: available-jobs.sh [positive-cap]" >&2; exit 2;; esac
[ "$cap" -gt 0 ] || exit 2

count_list() {
    awk -F, '{
        total = 0
        for (i = 1; i <= NF; i++) {
            if ($i ~ /^[0-9]+-[0-9]+$/) {
                split($i, range, "-")
                total += range[2] - range[1] + 1
            } else if ($i ~ /^[0-9]+$/) {
                total++
            }
        }
        print total
    }' 
}

available=$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf '1')
case "$available" in ''|*[!0-9]*) available=1;; esac
[ "$available" -gt 0 ] || available=1

# Respect the process CPU affinity mask when taskset is available.
if command -v taskset >/dev/null 2>&1; then
    affinity=$(taskset -pc "$$" 2>/dev/null | sed 's/.*: //')
    if [ -n "$affinity" ]; then
        affinity_count=$(printf '%s\n' "$affinity" | count_list)
        case "$affinity_count" in ''|*[!0-9]*) :;; *) [ "$affinity_count" -lt "$available" ] && available=$affinity_count;; esac
    fi
fi

# cgroup v2 quota: cpu.max contains quota period, or "max period".
if [ -r /sys/fs/cgroup/cpu.max ]; then
    read -r quota period < /sys/fs/cgroup/cpu.max || true
    if [ "${quota:-max}" != max ] && [ "${period:-0}" -gt 0 ] 2>/dev/null; then
        quota_cpus=$((quota / period))
        [ "$quota_cpus" -gt 0 ] || quota_cpus=1
        [ "$quota_cpus" -lt "$available" ] && available=$quota_cpus
    fi
elif [ -r /sys/fs/cgroup/cpu/cpu.cfs_quota_us ] &&
     [ -r /sys/fs/cgroup/cpu/cpu.cfs_period_us ]; then
    quota=$(cat /sys/fs/cgroup/cpu/cpu.cfs_quota_us)
    period=$(cat /sys/fs/cgroup/cpu/cpu.cfs_period_us)
    if [ "$quota" -gt 0 ] 2>/dev/null && [ "$period" -gt 0 ] 2>/dev/null; then
        quota_cpus=$((quota / period))
        [ "$quota_cpus" -gt 0 ] || quota_cpus=1
        [ "$quota_cpus" -lt "$available" ] && available=$quota_cpus
    fi
fi

# cgroup v2 effective cpuset, then v1 cpuset fallback.
for cpuset_file in /sys/fs/cgroup/cpuset.cpus.effective /sys/fs/cgroup/cpuset/cpuset.cpus; do
    if [ -r "$cpuset_file" ]; then
        cpuset=$(cat "$cpuset_file")
        if [ -n "$cpuset" ]; then
            cpuset_count=$(printf '%s\n' "$cpuset" | count_list)
            case "$cpuset_count" in ''|*[!0-9]*) :;; *) [ "$cpuset_count" -lt "$available" ] && available=$cpuset_count;; esac
            break
        fi
    fi
done

[ "$available" -gt "$cap" ] && available=$cap
[ "$available" -gt 0 ] || available=1
printf '%s\n' "$available"
