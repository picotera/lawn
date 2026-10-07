#!/usr/bin/env bash
#
# Regenerate every benchmark CSV behind the article's figures.
#
# The repository commits only the distinct-TTL crossover sweep
# (src/benchmarks/c/results/inflection.csv). The other figures' data is a
# build artifact that this script reproduces by running the harness, then
# rebuilds the figures from it. The huge tier reaches hundreds of millions
# of timers and wants far more RAM than the 24 GB laptop the paper was
# written on, so it belongs on the devpod.
#
# Run it ON the devpod (you are already there):
#
#     scripts/run-benchmarks.sh
#
# Or dispatch it to the devpod from your laptop (repo must already be
# checked out at $REMOTE_REPO on the target, default ~/lawn):
#
#     DEVPOD=my-workspace scripts/run-benchmarks.sh      # via: devpod ssh my-workspace --
#     SSH=user@host       scripts/run-benchmarks.sh      # via: ssh user@host
#
# Toggles (env vars, apply locally and carry across remote dispatch):
#
#     SKIP_HUGE=1   skip the hundreds-of-millions tier (sub-million only, minutes not hours)
#     NO_FIGURES=1  regenerate CSVs only, do not rebuild the PNGs
#     NO_TEST=1     skip the differential correctness gate (not recommended)
#     REMOTE_REPO=/path   repo location on the devpod (default: ~/lawn)
#
set -euo pipefail

SKIP_HUGE="${SKIP_HUGE:-0}"
NO_FIGURES="${NO_FIGURES:-0}"
NO_TEST="${NO_TEST:-0}"
REMOTE_REPO="${REMOTE_REPO:-\$HOME/lawn}"

# --- Remote dispatch: hand off to the same script on the devpod -------------
# DEVPOD/SSH are stripped from the remote environment so the dispatched copy
# runs locally there instead of bouncing onward.
if [ -n "${DEVPOD:-}" ] || [ -n "${SSH:-}" ]; then
  # $HOME stays literal in remote_cmd so it expands on the target, not here.
  remote_cmd="cd ${REMOTE_REPO} && SKIP_HUGE=${SKIP_HUGE} NO_FIGURES=${NO_FIGURES} NO_TEST=${NO_TEST} bash scripts/run-benchmarks.sh"
  if [ -n "${DEVPOD:-}" ]; then
    command -v devpod >/dev/null 2>&1 || { echo "error: 'devpod' CLI not found on PATH" >&2; exit 127; }
    echo ">> dispatching to devpod workspace '${DEVPOD}'"
    exec devpod ssh "${DEVPOD}" -- bash -lc "${remote_cmd}"
  else
    echo ">> dispatching over ssh to '${SSH}'"
    # ssh runs the single string in the target's login shell, which parses && and expands $HOME.
    exec ssh "${SSH}" "${remote_cmd}"
  fi
fi

# --- Local run (we are on the benchmark host) ------------------------------
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
BENCH_DIR="${REPO_ROOT}/src/benchmarks/c"
RESULTS_DIR="${BENCH_DIR}/results"

echo "== Lawn benchmark data regeneration =="
echo "   host:    $(uname -msr)"
echo "   repo:    ${REPO_ROOT}"
echo "   results: ${RESULTS_DIR}"
echo "   huge tier: $([ "${SKIP_HUGE}" = 1 ] && echo skipped || echo included)"
echo

step() { echo; echo ">> $* ($(date '+%H:%M:%S'))"; }

step "Building harness"
make -C "${BENCH_DIR}" clean
make -C "${BENCH_DIR}"

if [ "${NO_TEST}" != 1 ]; then
  step "Differential correctness gate (./test)"
  # Numbers from a run whose gate failed are not trustworthy, so stop here on failure.
  ( cd "${BENCH_DIR}" && ./test )
fi

# ./benchmark writes to ./results relative to its CWD, so run it from BENCH_DIR.
if [ "${SKIP_HUGE}" = 1 ]; then
  step "Benchmark sweeps (sub-million tier) + distribution + crossover"
  ( cd "${BENCH_DIR}" && ./benchmark sweeps && ./benchmark dist && ./benchmark inflection )
else
  step "Benchmark: full suite incl. hundreds-of-millions tier (hours, RAM-heavy)"
  echo "   (the huge tier is swap-sensitive, see the Measurement-ceiling limitation; run on a box with headroom)"
  ( cd "${BENCH_DIR}" && ./benchmark all )
fi

if [ "${NO_FIGURES}" != 1 ]; then
  step "Rebuilding figures from fresh CSVs (make figures)"
  make -C "${REPO_ROOT}/article" figures
fi

step "Done"
echo "   CSVs:    ${RESULTS_DIR}"
echo "   figures: ${REPO_ROOT}/article/*.png"
echo
echo "   Note: the concurrency numbers in the Limitations section come from a"
echo "   separate sweep of src/benchmarks/c/concurrent (not a figure); this"
echo "   script does not regenerate them."
