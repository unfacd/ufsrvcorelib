#!/usr/bin/env bash
#
# cpu_affinity_verify.sh — one-shot operational verification of a running
# ufsrv-class server's CPU affinity.
#
# Usage:
#   cpu_affinity_verify.sh [<process-name-or-pid>]
#
# Default process name: ufsrvwebsock.
#
# Three checks, in order (see UFSRV_THREADS_CPU_PINNING_DESIGN.md
# "Operational verification"):
#   1. module layout  — the "CPU affinity:" Describe JSON logged at startup
#   2. kernel affinity — each thread's Cpus_allowed_list
#   3. sanity          — policy vs observed affinity (advisory)
#
set -u

PROC="${1:-ufsrvwebsock}"

# ── resolve pid ───────────────────────────────────────────────────────────
if [[ "$PROC" =~ ^[0-9]+$ ]]; then
  PID="$PROC"
else
  PID=$(pidof "$PROC" 2>/dev/null | awk '{print $1}')
fi
if [[ -z "$PID" ]]; then
  echo "ERROR: no running process matching '$PROC'" >&2
  exit 1
fi

# number of logical CPUs on this host (for the "full mask" comparison)
NCPU=$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 0)

echo "=== $PROC (pid $PID, $NCPU logical cpus) ==="

# ── 1. module layout (Describe JSON) ──────────────────────────────────────
echo
echo "--- 1. module layout (last 'CPU affinity:' line) ---"
JSON=$(
  journalctl _PID="$PID" --no-pager 2>/dev/null | grep -o 'CPU affinity: .*' | tail -1
)
if [[ -z "$JSON" ]]; then
  JSON=$(grep -o "CPU affinity: .*${PROC}.*" /var/log/messages 2>/dev/null | tail -1)
fi
if [[ -z "$JSON" ]]; then
  echo "  (no 'CPU affinity:' line found — has the server logged it?)"
  POLICY=""
else
  echo "  $JSON"
  POLICY=$(echo "$JSON" | grep -o '"policy":"[^"]*"' | sed 's/.*"policy":"\([^"]*\)".*/\1/')
  CORES=$(echo "$JSON" | grep -o '"cores_sz":[0-9]*' | grep -o '[0-9]*')
  WORKERS=$(echo "$JSON" | grep -o '"worker_pool_sz":[0-9]*' | grep -o '[0-9]*')
  echo "  => policy=${POLICY:-?}  cores_sz=${CORES:-?}  worker_pool_sz=${WORKERS:-?}"
fi

# ── 2. kernel affinity (per thread) ───────────────────────────────────────
echo
echo "--- 2. thread affinity (comm -> Cpus_allowed_list) ---"
printf '%-20s %-16s %-12s %s\n' "TID" "comm" "placement" "cpus"
for tid in /proc/$PID/task/*; do
  t=${tid##*/}
  comm=$(cat "$tid/comm" 2>/dev/null)
  cpus=$(awk '/Cpus_allowed_list/{print $2}' "$tid/status" 2>/dev/null)

  # classify: single core == pinned; range/list == unpinned (full mask)
  if [[ "$cpus" =~ ^[0-9]+$ ]]; then
    placement="pinned→$cpus"
  else
    placement="unpinned"
  fi
  printf '%-20s %-16s %-12s %s\n' "$t" "$comm" "$placement" "$cpus"
done

# ── 3. sanity (advisory) ──────────────────────────────────────────────────
echo
echo "--- 3. sanity ---"
if [[ -z "$POLICY" ]]; then
  echo "  policy unknown — compare the list above against the expected mapping."
else
  case "$POLICY" in
    system_override)
      echo "  policy=system_override: EVERY thread should be 'unpinned' (full mask 0-$((NCPU-1)))."
      ;;
    managed)
      echo "  policy=managed: listener(main)=pinned→0, delegator=pinned→1, workers=pinned→2..N; timer/msgqueue=unpinned."
      ;;
    user_override)
      echo "  policy=user_override: listener=0, delegator=1, workers grouped (shared cores); timer/msgqueue=unpinned."
      ;;
    *) echo "  policy=$POLICY (unrecognised)";;
  esac
fi

exit 0
