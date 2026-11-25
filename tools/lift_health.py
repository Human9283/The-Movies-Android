#!/usr/bin/env python3
"""Health of a lift: what run_lift.py produced and what cannot resolve.

    python3 tools/lift_health.py ~/tm/work/gen [--json out.json]

Reports:
  * functions in the dispatch table and the lift's own counts (lift_stats.json
    next to gen/, if present);
  * unresolved ITAIL labels: an `L_XXXXXXXX: RECOMP_ITAIL` is a jump to code
    the body does not contain, resolved at run time through the dispatch
    table; a target with no dispatch entry can never resolve, and reaching it
    stops the run. Each is listed with the function that holds it;
  * RECOMP_NOT_LIFTED stubs (catalogued functions left out of the lift).
"""
import argparse
import glob
import json
import os
import re
import sys

FUNC_RE = re.compile(r'^void (sub_[0-9A-F]{8})\(void\)', re.M)
ITAIL_RE = re.compile(r'L_([0-9A-F]{8}): RECOMP_ITAIL')
DISP_RE = re.compile(r'\{ 0x([0-9A-F]{8})u,')


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('gen')
    ap.add_argument('--json')
    a = ap.parse_args(argv)

    disp = set(DISP_RE.findall(open(os.path.join(a.gen, 'recomp_dispatch.c')).read()))
    unresolved, not_lifted, files, lines = [], 0, 0, 0
    for fn in sorted(glob.glob(os.path.join(a.gen, 'recomp_0*.c'))):
        src = open(fn).read()
        files += 1
        lines += src.count('\n')
        not_lifted += src.count('RECOMP_NOT_LIFTED(0x')
        # attribute each ITAIL label to the function body it sits in
        starts = [(m.start(), m.group(1)) for m in FUNC_RE.finditer(src)]
        for m in ITAIL_RE.finditer(src):
            tgt = m.group(1)
            if tgt in disp:
                continue
            owner = None
            for pos, name in starts:
                if pos > m.start():
                    break
                owner = name
            unresolved.append({'target': '0x' + tgt, 'in': owner, 'file': os.path.basename(fn)})

    stats_path = os.path.join(os.path.dirname(os.path.abspath(a.gen)), 'lift_stats.json')
    stats = json.load(open(stats_path)) if os.path.exists(stats_path) else {}
    out = {'dispatch_entries': len(disp), 'files': files, 'lines': lines,
           'not_lifted_stubs': not_lifted, 'unresolved_itail': unresolved, 'lift_stats': stats}
    print('dispatch entries : %d' % len(disp))
    print('generated files  : %d, %d lines' % (files, lines))
    if stats:
        print('lift_stats       : lifted %(lifted)d, errors %(errors)d, no terminator %(no_terminator)d' % stats)
    print('not-lifted stubs : %d' % not_lifted)
    print('unresolved ITAIL : %d' % len(unresolved))
    for u in unresolved:
        print('  %s  in %s  (%s)' % (u['target'], u['in'], u['file']))
    if a.json:
        json.dump(out, open(a.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
