#!/usr/bin/env python3
"""
Write the list of (configuration, trace) simulations for the SLURM array job.

Usage:
  python3 experiments/baseline/make_jobs.py --traces traces.txt [--configs bl01 bl07 ...]
                                            [--warmup 50000000] [--sim 100000000] [--out jobs.txt]

traces.txt lists one trace path per line (blank lines and lines starting with # are ignored).
Each line of the output is: <binary> <trace> <result stem> <warmup> <sim>
Simulations whose result JSON already exists are skipped, so the script can be rerun to resubmit only what is missing.
"""

import argparse
import csv
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))


def trace_stem(path):
    name = os.path.basename(path)
    for suffix in ('.champsimtrace.xz', '.champsimtrace.gz', '.trace.xz', '.trace.gz', '.xz', '.gz'):
        if name.endswith(suffix):
            return name[:-len(suffix)]
    return name


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--traces', required=True, help='file listing one trace path per line')
    parser.add_argument('--configs', nargs='*', help='only these configurations (names or id prefixes such as bl01); default: all in manifest.csv')
    parser.add_argument('--warmup', type=int, default=50_000_000)
    parser.add_argument('--sim', type=int, default=100_000_000)
    parser.add_argument('--results', default=os.path.join(HERE, 'results'))
    parser.add_argument('--out', default=os.path.join(HERE, 'jobs.txt'))
    args = parser.parse_args()

    with open(os.path.join(HERE, 'configs', 'manifest.csv')) as f:
        names = [row['name'] for row in csv.DictReader(f)]
    if args.configs:
        names = [n for n in names if any(n == c or n.startswith(c + '_') for c in args.configs)]

    with open(args.traces) as f:
        traces = [os.path.abspath(line.strip()) for line in f if line.strip() and not line.lstrip().startswith('#')]

    missing_traces = [t for t in traces if not os.path.exists(t)]
    if missing_traces:
        print(f'Warning: {len(missing_traces)} trace(s) not found, e.g. {missing_traces[0]}')

    lines, skipped = [], 0
    for name in names:
        binary = os.path.join(REPO, 'bin', name)
        if not os.path.exists(binary):
            print(f'Warning: {binary} has not been built')
        for trace in traces:
            stem = os.path.join(os.path.abspath(args.results), name, trace_stem(trace))
            if os.path.exists(stem + '.json'):
                skipped += 1
                continue
            lines.append(f'{binary} {trace} {stem} {args.warmup} {args.sim}')

    with open(args.out, 'w') as f:
        f.write('\n'.join(lines) + ('\n' if lines else ''))

    print(f'{len(names)} configurations x {len(traces)} traces: {len(lines)} jobs written to {args.out} ({skipped} already done)')
    if lines:
        print(f'Submit with: sbatch --array=1-{len(lines)}%100 experiments/baseline/run_array.slurm')


if __name__ == '__main__':
    main()
