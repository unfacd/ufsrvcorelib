#!/usr/bin/env bash
# dev/scripts/dev_build.sh — configure → build (+ optional test).
#
# Fast development loop: configure the repo with the chosen CMake preset, build
# it, and with --test run the harness as well.  No install, verify, or push —
# this is the inner loop (the release pipeline lives in build_install_verify.sh).
#
# Usage:
#   dev/scripts/dev_build.sh [PRESET] [--fresh] [--test] [--jobs N] [--dry-run] [--help]
#
# PRESET (default: dev) selects the CMake preset and names build/<PRESET>
# (or build/<PRESET>-test when --test is given, so a tests-ON build never
# disturbs the tests-OFF build directory).
#   --fresh  fresh configure (cmake --fresh): full clean rebuild
#   --test   build the gtest harness (-D_PACKAGE_TESTS=ON) in build/<PRESET>-test
#            and run it with ctest
#
# Exit status: 0 on success; non-zero if any stage fails (set -euo pipefail).
set -euo pipefail

# ── Defaults ────────────────────────────────────────────────────────────────

PRESET="dev"
JOBS="$(nproc)"
FRESH=0
RUN_TESTS=0
DRY_RUN=0

usage() {
    cat <<EOF
Usage: $0 [PRESET] [options]

Configure and build (no install / verify / push).

  $0 dev              # configure + build the 'dev' preset
  $0 dev --test       # same, plus the gtest harness built and run

Options:
  --preset NAME  Explicit preset (same as the positional PRESET)
  --jobs N       Parallel build jobs (default: \$(nproc))
  --fresh        Fresh configure (cmake --fresh): full clean rebuild
  --test         Build the gtest harness (-D_PACKAGE_TESTS=ON) in
                 build/<preset>-test and run it with ctest
  --dry-run      Print commands without executing them
  -h, --help     Show this help
EOF
}

# ── Argument parsing ────────────────────────────────────────────────────────

while [ $# -gt 0 ]; do
    case "$1" in
        --preset)  PRESET="$2"; shift 2 ;;
        --jobs)    JOBS="$2"; shift 2 ;;
        --fresh)   FRESH=1; shift ;;
        --test)    RUN_TESTS=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage; exit 0 ;;
        --*)       echo "error: unknown option '$1'" >&2; usage >&2; exit 2 ;;
        *)         PRESET="$1"; shift ;;
    esac
done

if [ "$RUN_TESTS" -eq 1 ]; then
    BUILD_DIR="build/${PRESET}-test"
    TOTAL_STEPS=3
else
    BUILD_DIR="build/${PRESET}"
    TOTAL_STEPS=2
fi

# ── Helpers ─────────────────────────────────────────────────────────────────

step() { printf '\n==> %s\n' "$*"; }
run() {
    printf '+ %s\n' "$*"
    if [ "$DRY_RUN" -eq 0 ]; then
        "$@"
    fi
}

printf '==> dev_build: preset=%s build=%s tests=%s fresh=%s jobs=%s\n' \
    "$PRESET" "$BUILD_DIR" \
    "$([ "$RUN_TESTS" -eq 1 ] && echo ON || echo OFF)" \
    "$([ "$FRESH" -eq 1 ] && echo yes || echo no)" \
    "$JOBS"

# ── 1. Configure ────────────────────────────────────────────────────────────

step "[1/$TOTAL_STEPS] Configure (preset=$PRESET, build=$BUILD_DIR)"
configure_args=(--preset "$PRESET" -B "$BUILD_DIR")
if [ "$FRESH" -eq 1 ]; then
    configure_args=(--fresh "${configure_args[@]}")
fi
if [ "$RUN_TESTS" -eq 1 ]; then
    configure_args+=(-D_PACKAGE_TESTS=ON)
fi
run cmake "${configure_args[@]}"

# ── 2. Build ────────────────────────────────────────────────────────────────

step "[2/$TOTAL_STEPS] Build ($JOBS jobs)"
run cmake --build "$BUILD_DIR" -j"$JOBS"

# ── 3. Test (only with --test) ──────────────────────────────────────────────

if [ "$RUN_TESTS" -eq 1 ]; then
    step "[3/$TOTAL_STEPS] Test"
    run ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

printf '\n==> done\n'
