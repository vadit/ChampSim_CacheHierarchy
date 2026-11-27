#!/usr/bin/env python3
"""
Collect baseline results into one CSV, and optionally select memory-intensive traces.

Usage:
  python3 experiments/baseline/collect_results.py [--traces traces.txt] [--results DIR] [--out results.csv]
  python3 experiments/baseline/collect_results.py --select-traces --config bl01 --min-llc-mpki 1.0

With --traces, only results for the traces listed in that file are kept, and any configuration that is missing
one of them is reported, so the CSV contains exactly the final trace set and every average uses the same traces.

Reads results/<config>/<trace>.json (ChampSim --json output) and writes one row per simulation with:
  IPC, LLC and L2 demand MPKI, back-invalidations per kilo-instruction, and speedup over the LRU/LRU
  configuration of the same hierarchy mode and L2 size.

Demand MPKI counts LOAD and RFO misses only (not writebacks or prefetches).
"""

import argparse
import csv
import glob
import json
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))

from make_jobs import trace_stem  # same naming as the result files, so the two scripts always agree


NAME_PATTERN = re.compile(r'^bl(?P<id>\d+)_(?P<mode>ni|incl)_l2(?P<l2_policy>[a-z_]+?)(?P<l2_kb>\d+)k_llc(?P<llc_policy>.+)$')


def load_manifest():
    path = os.path.join(HERE, 'configs', 'manifest.csv')
    if not os.path.exists(path):
        return {}
    with open(path) as f:
        return {row['name']: row for row in csv.DictReader(f)}


def config_metadata(config, manifest):
    """
    Metadata for a configuration, from manifest.csv or, failing that, from the configuration name itself.
    Regenerating the configurations (e.g. with --qbs-l2 srrip) replaces manifest.csv, so older results
    would otherwise lose their metadata.
    """
    if config in manifest:
        return manifest[config]
    m = NAME_PATTERN.match(config)
    if not m:
        return {}
    return {'mode': 'inclusive' if m['mode'] == 'incl' else 'non-inclusive', 'l2_policy': m['l2_policy'], 'l2_kb': m['l2_kb'], 'llc_policy': m['llc_policy']}


def find_cache(roi, suffix):
    """Return the stats of the first cache whose name ends with suffix (e.g. 'L2C' matches 'cpu0_L2C')."""
    for name, stats in roi.items():
        if isinstance(stats, dict) and name.endswith(suffix) and 'LOAD' in stats:
            return stats
    return None


def demand_misses(cache):
    return sum(sum(cache[t]['miss']) for t in ('LOAD', 'RFO') if t in cache)


def parse(path):
    with open(path) as f:
        phases = json.load(f)
    roi = phases[-1]['roi']  # the last phase is the measured region
    core = roi['cores'][0]
    instrs, cycles = core['instructions'], core['cycles']
    kilo = instrs / 1000.0

    llc = roi.get('LLC')
    l2 = find_cache(roi, 'L2C')
    row = {
        'instructions': instrs,
        'cycles': cycles,
        'ipc': instrs / cycles if cycles else float('nan'),
        'llc_demand_mpki': demand_misses(llc) / kilo if llc else float('nan'),
        'l2_demand_mpki': demand_misses(l2) / kilo if l2 else float('nan'),
        'llc_back_inval_pki': llc.get('back-invalidations issued', 0) / kilo if llc else float('nan'),
    }
    return row


def collect(results_dir, manifest):
    rows = []
    for path in sorted(glob.glob(os.path.join(results_dir, '*', '*.json'))):
        config = os.path.basename(os.path.dirname(path))
        trace = os.path.splitext(os.path.basename(path))[0]
        try:
            row = parse(path)
        except (OSError, ValueError, KeyError, IndexError) as e:
            print(f'Skipping {path}: {e}')
            continue
        meta = config_metadata(config, manifest)
        rows.append({'config': config, 'trace': trace, 'mode': meta.get('mode', ''), 'l2_policy': meta.get('l2_policy', ''),
                     'l2_kb': meta.get('l2_kb', ''), 'llc_policy': meta.get('llc_policy', ''), **row})
    return rows


def read_trace_list(path):
    with open(path) as f:
        return [trace_stem(line.strip()) for line in f if line.strip() and not line.lstrip().startswith('#')]


def filter_to_traces(rows, wanted, expected_configs=()):
    """
    Keep only rows for the wanted traces, and report configurations that are missing any of them,
    including configurations in the manifest that have no results at all.
    """
    wanted_set = set(wanted)
    kept = [r for r in rows if r['trace'] in wanted_set]
    dropped = len(rows) - len(kept)

    have = {config: set() for config in expected_configs}
    for r in kept:
        have.setdefault(r['config'], set()).add(r['trace'])
    incomplete = {config: sorted(wanted_set - traces) for config, traces in sorted(have.items()) if wanted_set - traces}

    print(f'Kept {len(kept)} rows for {len(wanted_set)} traces ({dropped} rows for other traces dropped)')
    for config, missing in incomplete.items():
        shown = ', '.join(missing[:5]) + (f', ... ({len(missing)} total)' if len(missing) > 5 else '')
        print(f'Warning: {config} is missing {len(missing)} of {len(wanted_set)} traces: {shown}')
    return kept


def add_speedups(rows):
    """Speedup over the LRU/LRU configuration with the same mode and L2 size, on the same trace."""
    baseline = {(r['mode'], r['l2_kb'], r['trace']): r['ipc'] for r in rows if r['l2_policy'] == 'lru' and r['llc_policy'] == 'lru'}
    for r in rows:
        base = baseline.get((r['mode'], r['l2_kb'], r['trace']))
        r['speedup_vs_lru_lru'] = r['ipc'] / base if base else float('nan')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--results', default=os.path.join(HERE, 'results'))
    parser.add_argument('--out', default=os.path.join(HERE, 'results.csv'))
    parser.add_argument('--traces', help='keep only the traces listed in this file (one path per line), and report configurations missing any of them')
    parser.add_argument('--select-traces', action='store_true', help='print the traces whose LLC demand MPKI meets --min-llc-mpki under --config')
    parser.add_argument('--config', default='bl01', help='configuration used for trace selection (default: bl01, non-inclusive LRU/LRU)')
    parser.add_argument('--min-llc-mpki', type=float, default=1.0)
    args = parser.parse_args()

    manifest = load_manifest()
    rows = collect(args.results, manifest)
    if args.traces:
        rows = filter_to_traces(rows, read_trace_list(args.traces), expected_configs=manifest.keys())
    if not rows:
        print(f'No results found in {args.results}')
        return

    if args.select_traces:
        chosen = [r for r in rows if (r['config'] == args.config or r['config'].startswith(args.config + '_')) and r['llc_demand_mpki'] >= args.min_llc_mpki]
        for r in sorted(chosen, key=lambda r: -r['llc_demand_mpki']):
            print(f"{r['trace']}\t{r['llc_demand_mpki']:.2f}")
        print(f'{len(chosen)} traces with LLC demand MPKI >= {args.min_llc_mpki}')
        return

    add_speedups(rows)
    with open(args.out, 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f'Wrote {len(rows)} rows to {args.out}')


if __name__ == '__main__':
    main()
