#!/bin/bash
# Build one ChampSim binary per baseline configuration.
#
# Usage (from anywhere):
#   bash experiments/baseline/build.sh [-j N] [config.json ...]
#
# With no configuration files, builds every file in experiments/baseline/configs/.
# Binaries are written to bin/<executable_name>, e.g. bin/bl01_ni_l2lru512k_llclru.
# Each configuration is configured and built in turn, because config.sh rewrites the shared build files.

set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

JOBS="${SLURM_CPUS_PER_TASK:-$(nproc)}"
if [[ "${1:-}" == "-j" ]]; then
  JOBS="$2"
  shift 2
fi

if [[ $# -gt 0 ]]; then
  CONFIGS=("$@")
else
  CONFIGS=("$HERE"/configs/bl*.json)
fi

if [[ ! -e "${CONFIGS[0]}" ]]; then
  echo "No configurations found. Run: python3 experiments/baseline/gen_configs.py" >&2
  exit 1
fi

cd "$REPO"
mkdir -p "$HERE/logs"
for cfg in "${CONFIGS[@]}"; do
  cfg="$(cd "$(dirname "$cfg")" && pwd)/$(basename "$cfg")"
  name="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["executable_name"])' "$cfg")"
  echo "=== Building $name"
  ./config.sh "$cfg" > /dev/null
  if make -j "$JOBS" > "$HERE/logs/build_${name}.log" 2>&1; then
    echo "    ok: bin/$name"
  else
    echo "    FAILED (see experiments/baseline/logs/build_${name}.log)"
  fi
done 2>&1 | tee "$HERE/logs/build_summary.log"

if grep -q FAILED "$HERE/logs/build_summary.log"; then
  exit 1
fi
