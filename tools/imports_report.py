#!/usr/bin/env python3
"""The table of imports the game calls, for docs/IMPORTS.md.

    python3 tools/imports_report.py PRE.json FULL.json

PRE.json is an --import-trace of a --stop-at-winmain run, FULL.json one of a
run without it; both from the game directory. Prints a Markdown table: every
import called in FULL, its status, its call count, whether it is first called
before or after WinMain, and the lifted function of its first call.
"""
import json
import sys


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    pre = {i['name'] for i in json.load(open(argv[1]))['imports']}
    full = json.load(open(argv[2]))
    rows = sorted(full['imports'], key=lambda i: (i['name'] not in pre, i['status'], i['name'].lower()))
    print('%d imports bound, %d called (%d before WinMain).\n' % (full['bound'], full['called'], len(pre)))
    print('| Import | Status | Calls | First called | First caller |')
    print('|---|---|---:|---|---|')
    for i in rows:
        print('| `%s` | %s | %d | %s | `%s` |' % (i['name'], i['status'], i['calls'],
                                               'before WinMain' if i['name'] in pre else 'after WinMain',
                                               i['first_calls'][0]['caller']))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
