#!/bin/bash
# SLURM array job: each task runs one (configuration, trace) simulation from jobs.txt.
#
# Submit from the repository root, with the array size printed by make_jobs.py:
#   sbatch --array=1-<N>%100 experiments/baseline/run_array.slurm
# (%100 caps the number of simulations running at once; adjust to your allocation.)
#
# Results: experiments/baseline/results/<config>/<trace>.json (stats) and .log (plain output).

##NECESSARY JOB SPECIFICATIONS
#SBATCH --job-name=champsim-baseline
#SBATCH --time=02:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=1
#SBATCH --mem=4G
#SBATCH --output=experiments/baseline/logs/slurm_%A_%a.out

##OPTIONAL JOB SPECIFICATIONS
##SBATCH --account=123456

set -euo pipefail

# Grace modules (check with: module spider GCCcore). Skipped when not on a cluster.
if command -v module > /dev/null 2>&1; then
  module purge
  module load GCCcore/13.3.0
fi

JOBS_FILE="${JOBS_FILE:-experiments/baseline/jobs.txt}"
LINE="$(sed -n "${SLURM_ARRAY_TASK_ID}p" "$JOBS_FILE")"
if [[ -z "$LINE" ]]; then
  echo "No job on line ${SLURM_ARRAY_TASK_ID} of $JOBS_FILE" >&2
  exit 1
fi
read -r BINARY TRACE STEM WARMUP SIM <<< "$LINE"

if [[ -s "${STEM}.json" ]]; then
  echo "Already done: ${STEM}.json"
  exit 0
fi
mkdir -p "$(dirname "$STEM")"

echo "Host: $(hostname)  Start: $(date)"
echo "Binary: $BINARY"
echo "Trace:  $TRACE"

# Write to temporary names, and rename only on success, so a killed job never leaves a partial result
"$BINARY" --warmup-instructions "$WARMUP" --simulation-instructions "$SIM" --json "${STEM}.json.tmp" "$TRACE" > "${STEM}.log.tmp" 2>&1
mv "${STEM}.log.tmp" "${STEM}.log"
mv "${STEM}.json.tmp" "${STEM}.json"

echo "End: $(date)"
