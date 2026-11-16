#!/bin/bash
# Build every baseline configuration on a compute node.
#
# Submit from the repository root:
#   sbatch experiments/baseline/build.slurm
#
# vcpkg dependencies must already be installed (see experiments/baseline/README.md), because compute nodes may not
# have internet access.

##NECESSARY JOB SPECIFICATIONS
#SBATCH --job-name=champsim-build
#SBATCH --time=02:00:00
#SBATCH --ntasks=1
#SBATCH --cpus-per-task=16
#SBATCH --mem=16G
#SBATCH --output=experiments/baseline/logs/build_%j.out

##OPTIONAL JOB SPECIFICATIONS
##SBATCH --account=123456

set -euo pipefail

# Grace modules (check with: module spider GCCcore). Skipped when not on a cluster.
if command -v module > /dev/null 2>&1; then
  module purge
  module load GCCcore/13.3.0 Python/3.12.3
fi

bash experiments/baseline/build.sh -j "$SLURM_CPUS_PER_TASK"
