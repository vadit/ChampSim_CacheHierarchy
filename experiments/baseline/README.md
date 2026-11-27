# Baseline configuration matrix

Scripts to build and run the baseline matrix on TAMU Grace (SLURM). They also run on any Linux machine, where the
SLURM scripts can be executed directly with `SLURM_ARRAY_TASK_ID` set by hand.

| # | Hierarchy | L2 | L2 size | LLC |
|---|---|---|---|---|
| 1–6 | non-inclusive | LRU, SRRIP | 512 KB | LRU, SRRIP, Mockingjay |
| 7–10 | inclusive | LRU, SRRIP | 512 KB | LRU, SRRIP |
| 11–12 | inclusive | best L2 from 7–10 (`--qbs-l2`) | 512 KB | QBS+LRU, QBS+SRRIP |
| 13–15 | non-inclusive | LRU | 1 MB | LRU, SRRIP, Mockingjay |
| 16–17 | inclusive | LRU | 1 MB | LRU, SRRIP |
| 18–19 | non-inclusive, inclusive | LRU | 512 KB | SHiP (skip with `--no-ship`) |
| 20 | inclusive | best L2 from 7–10 (`--qbs-l2`) | 512 KB | QBS+SHiP |

All configurations are single core with no prefetcher and a 2 MB, 16-way LLC. Mockingjay is only run non-inclusive,
because an inclusive LLC suppresses its bypasses.

## Files

| File | Purpose |
|---|---|
| `gen_configs.py` | Writes `configs/<name>.json` for every configuration, plus `configs/manifest.csv` |
| `build.sh` / `build.slurm` | Builds `bin/<name>` for every configuration (locally / as a SLURM job) |
| `make_jobs.py` | Writes `jobs.txt`, one line per (configuration, trace); skips simulations that already have results |
| `run_array.slurm` | SLURM array job; task *i* runs line *i* of `jobs.txt` |
| `collect_results.py` | Collects `results/*/*.json` into `results.csv`, or selects memory-intensive traces |

Results are written to `results/<config>/<trace>.json` (ChampSim JSON statistics) and `.log` (plain output).
A simulation writes to temporary files and renames them only when it finishes, so rerunning `make_jobs.py`
after a failure or timeout resubmits only the missing simulations.

## Workflow on Grace

### 1. One-time setup (login node)

Work in `$SCRATCH`, since the traces and results are large.

```bash
cd $SCRATCH
git clone <your fork> ChampSim_CacheHierarchy && cd ChampSim_CacheHierarchy
git submodule update --init
module load GCCcore/13.3.0 Python/3.12.3
vcpkg/bootstrap-vcpkg.sh
vcpkg/vcpkg install
```

The vcpkg step downloads the dependencies, so do it on a login node; compute nodes may not have internet access.
If `GCCcore/13.3.0` is not available, `module spider GCCcore` lists the installed versions.

### 2. Get candidate traces

Download the SPEC 2017 (DPC-3) and SPEC 2006 (CRC-2) ChampSim traces into, for example, `$SCRATCH/traces/`, and
list them one per line in `experiments/baseline/candidate_traces.txt`.

### 3. Generate and build the configurations

```bash
python3 experiments/baseline/gen_configs.py
sbatch experiments/baseline/build.slurm
```

Check `experiments/baseline/logs/build_summary.log` when the job finishes; every line should say `ok`.

### 4. Select memory-intensive traces

Run the LRU/LRU baseline on every candidate trace, then keep the ones with an LLC demand MPKI of at least 1:

```bash
python3 experiments/baseline/make_jobs.py --traces experiments/baseline/candidate_traces.txt --configs bl01
sbatch --array=1-<N>%100 experiments/baseline/run_array.slurm      # N is printed by make_jobs.py
# when the jobs finish:
python3 experiments/baseline/collect_results.py --select-traces --config bl01 --min-llc-mpki 1.0
```

Put about 30 of the selected trace paths in `experiments/baseline/traces.txt`, and record the design/held-out split
(about 20/10) now, before looking at any other results. The `bl01` results are reused in the next step.

### 5. Run the matrix

```bash
python3 experiments/baseline/make_jobs.py --traces experiments/baseline/traces.txt
sbatch --array=1-<N>%100 experiments/baseline/run_array.slurm
```

### 6. Fix the QBS configurations

QBS (11, 12 and 20) is paired with the L2 policy of the best inclusive pair from 7–10. The default is LRU. If SRRIP at the
L2 turns out better, regenerate and rerun only those three:

```bash
python3 experiments/baseline/gen_configs.py --qbs-l2 srrip
bash experiments/baseline/build.sh experiments/baseline/configs/bl11_*.json experiments/baseline/configs/bl12_*.json experiments/baseline/configs/bl20_*.json
python3 experiments/baseline/make_jobs.py --traces experiments/baseline/traces.txt --configs bl11 bl12 bl20
sbatch --array=1-<N>%100 experiments/baseline/run_array.slurm
```

The configuration names include the L2 policy, so the LRU and SRRIP versions keep separate results.

### 7. Collect

```bash
python3 experiments/baseline/collect_results.py --traces experiments/baseline/traces.txt
```

`--traces` keeps only the final trace set, dropping results left over from trace selection or earlier trace lists,
so every average uses the same traces. It also warns about any configuration that is missing one of those traces,
including configurations with no results at all.

`results.csv` has one row per simulation: IPC, LLC and L2 demand MPKI (LOAD and RFO misses only), LLC
back-invalidations per kilo-instruction, and speedup over LRU/LRU in the same hierarchy mode and L2 size.

## Cost

With a 50M-instruction warmup and a 100M-instruction window, a run takes about 15 minutes. With SHiP and QBS+SHiP
included, 20 configurations × 23 traces = 460 runs, or about 115 CPU-hours. Trace selection adds one run per candidate trace.
