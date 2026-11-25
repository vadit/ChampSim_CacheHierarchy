#!/usr/bin/env python3
"""
Generate the ChampSim configuration files for the baseline matrix.

Matrix (single core, no prefetcher, 2 MB LLC):
  Main (512 KB L2):
     1-6   non-inclusive  L2 {LRU, SRRIP} x LLC {LRU, SRRIP, Mockingjay}
     7-10  inclusive      L2 {LRU, SRRIP} x LLC {LRU, SRRIP}
  QBS (inclusive only):
    11-12  inclusive      L2 <--qbs-l2>   x LLC {QBS+LRU, QBS+SRRIP}
  L2-size sweep (1 MB L2, LRU at the L2):
    13-15  non-inclusive  LLC {LRU, SRRIP, Mockingjay}
    16-17  inclusive      LLC {LRU, SRRIP}
  SHiP (optional, --no-ship to skip):
    18-19  {non-inclusive, inclusive}  L2 LRU x LLC SHiP
    20     inclusive      L2 <--qbs-l2>   x LLC QBS+SHiP

Mockingjay is only run non-inclusive, because an inclusive cache suppresses its bypasses.

Usage:
  python3 experiments/baseline/gen_configs.py [--qbs-l2 lru|srrip] [--no-ship]

Writes experiments/baseline/configs/<name>.json and experiments/baseline/configs/manifest.csv.
"""

import argparse
import copy
import csv
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))

DEFAULT_L2_SETS = 1024  # 1024 sets x 8 ways x 64 B = 512 KB
LARGE_L2_SETS = 2048    # 1 MB


def matrix(qbs_l2, include_ship):
    """Yield (id, mode, l2_policy, l2_sets, llc_policy) for each baseline configuration."""
    n = 0

    def nxt():
        nonlocal n
        n += 1
        return n

    # Main matrix
    for l2 in ('lru', 'srrip'):
        for llc in ('lru', 'srrip', 'mockingjay'):
            yield nxt(), 'non-inclusive', l2, DEFAULT_L2_SETS, llc
    for l2 in ('lru', 'srrip'):
        for llc in ('lru', 'srrip'):
            yield nxt(), 'inclusive', l2, DEFAULT_L2_SETS, llc

    # QBS
    for llc in ('qbs_lru', 'qbs_srrip'):
        yield nxt(), 'inclusive', qbs_l2, DEFAULT_L2_SETS, llc

    # L2-size sweep
    for llc in ('lru', 'srrip', 'mockingjay'):
        yield nxt(), 'non-inclusive', 'lru', LARGE_L2_SETS, llc
    for llc in ('lru', 'srrip'):
        yield nxt(), 'inclusive', 'lru', LARGE_L2_SETS, llc

    # SHiP, and QBS on SHiP (numbered last so the IDs of the earlier configurations do not change)
    if include_ship:
        for mode in ('non-inclusive', 'inclusive'):
            yield nxt(), mode, 'lru', DEFAULT_L2_SETS, 'ship'
        yield nxt(), 'inclusive', qbs_l2, DEFAULT_L2_SETS, 'qbs_ship'


def config_name(cid, mode, l2, l2_sets, llc):
    mode_tag = 'ni' if mode == 'non-inclusive' else 'incl'
    size_tag = f'{l2_sets * 8 * 64 // 1024}k'
    return f'bl{cid:02d}_{mode_tag}_l2{l2}{size_tag}_llc{llc}'


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--base', default=os.path.join(REPO, 'champsim_config.json'), help='base configuration (default: the repository champsim_config.json)')
    parser.add_argument('--out', default=os.path.join(HERE, 'configs'), help='output directory')
    parser.add_argument('--qbs-l2', choices=('lru', 'srrip'), default='lru',
                        help='L2 policy for the QBS configurations (11, 12, 20); set this to the L2 policy of the best inclusive pair from configs 7-10')
    parser.add_argument('--no-ship', action='store_true', help='skip the SHiP configurations (18-20)')
    args = parser.parse_args()

    with open(args.base) as f:
        base = json.load(f)

    os.makedirs(args.out, exist_ok=True)
    rows = []
    for cid, mode, l2, l2_sets, llc in matrix(args.qbs_l2, not args.no_ship):
        name = config_name(cid, mode, l2, l2_sets, llc)
        cfg = copy.deepcopy(base)
        cfg['executable_name'] = name
        cfg['inclusion_policy'] = mode
        cfg['num_cores'] = 1
        cfg.setdefault('L2C', {})
        cfg['L2C']['sets'] = l2_sets
        cfg['L2C']['replacement'] = l2
        cfg['L2C']['prefetcher'] = 'no'
        cfg.setdefault('LLC', {})
        cfg['LLC']['replacement'] = llc
        cfg['LLC']['prefetcher'] = 'no'
        cfg.setdefault('L1D', {})['prefetcher'] = 'no'
        cfg.setdefault('L1I', {})['prefetcher'] = 'no'

        # The hierarchy-wide key sets every data cache; make sure no per-cache value overrides it
        for cache in ('L1I', 'L1D', 'L2C', 'LLC'):
            cfg[cache].pop('inclusion_policy', None)

        path = os.path.join(args.out, f'{name}.json')
        with open(path, 'w') as f:
            json.dump(cfg, f, indent=2)
            f.write('\n')
        rows.append({'id': cid, 'name': name, 'mode': mode, 'l2_policy': l2, 'l2_kb': l2_sets * 8 * 64 // 1024, 'llc_policy': llc})

    with open(os.path.join(args.out, 'manifest.csv'), 'w', newline='') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)

    for r in rows:
        print(f"{r['id']:2d}  {r['mode']:<14} L2 {r['l2_policy']:<6} {r['l2_kb']:>5} KB  LLC {r['llc_policy']:<11} -> {r['name']}")
    print(f'Wrote {len(rows)} configurations to {args.out}')


if __name__ == '__main__':
    main()
