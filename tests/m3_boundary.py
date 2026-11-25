#!/usr/bin/env python3
r"""M3 regression test: the run past WinMain, to the current execution boundary.

    python3 tests/m3_boundary.py --host ~/tm/build/tm_host --game ~/tm/game
    python3 tests/m3_boundary.py ... --trace-out docs/traces/m3_boundary_imports.json
    python3 tests/m3_boundary.py ... --update      # accept a reviewed baseline

Runs `tm_host <game>` (no --stop-at-winmain) from the game directory with a
fresh user directory holding an empty `My Documents` (the main M2 scenario), with HLE_FILETRACE=1 and --import-trace, until the run stops.
Compared with tests/baselines/m3_boundary.json:

  * the stop: the exit code and the boundary markers, in order (the last
    lines that say where and why the run ends);
  * no delay-load failure (`[except] delay-load failure`), except of a DLL
    listed in BOUNDARY_DLLS: one that is not implemented yet and whose
    failed load is the boundary itself; no `[not-lifted]`, no unresolved
    ICALL/ITAIL;
  * the DirectInput calls, each with its result, in order;
  * the DirectShow objects (`[dshow]`: creation, the graph, destruction);
  * the host-DLL loads (`[loader] LoadLibraryA ... host DLL`);
  * every import called, by name (a new one or a missing one fails);
  * the game directory unchanged (file list, sizes, modification times);
  * what the run created in the user directory.

A baseline is written only by --update, from a run that stops at a
recognised boundary. Exit 0 on a pass, 1 otherwise.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from m2_winmain import listing, snapshot  # noqa: E402

BASELINE = os.path.join(HERE, 'baselines', 'm3_boundary.json')
# DLLs whose delay-load failure is the boundary, not a defect: not
# implemented yet, by plan. d3d9.dll: Direct3D 9 is M3 step 4 (docs/M3.md).
BOUNDARY_DLLS = {'d3d9.dll'}
DELAY_DLL = re.compile(r'^\[except\]   dll "([^"]+)"')
BAD = [('delay-load failure', re.compile(r'\[except\] delay-load failure')),
       ('function not lifted', re.compile(r'\[not-lifted\]')),
       ('unresolved indirect call', re.compile(r'ICALL: unresolved')),
       ('unresolved jump', re.compile(r'ITAIL'))]
# Lines that mark where the run is: kept in order, and the last ones compared.
MARK = re.compile(r'^(\[poc\] WinMain|\[MessageBoxW\] type|\[loader\]|\[module\] LoadLibraryA|'
                  r'\[file\] FindFirstFileA "C:\\WINDOWS|\[kernel32\] GetTempPathA|=== |  in lifted |'
                  r'\[win32hle\] FATAL|\[except\] RaiseException|\[watchdog\]|\[ole32\] CoCreateInstance)')


def normalise(line):
    """Drop what changes from run to run: times, thread ids, host paths."""
    line = re.sub(r'\d+(\.\d+)? ms', 'N ms', line)
    line = re.sub(r'thread \d+', 'thread T', line)
    line = re.sub(r'entered: N ms, ', 'entered: ', line)
    line = re.sub(r'^(\[file\] FindFirstFileA "[^"]*") in /\S+$', r'\1: found', line)   # the temporary host directory
    line = re.sub(r'(\[MessageBoxW\] type 0x[0-9A-Fa-f]+) caption .*', r'\1', line)   # no game text in the baseline
    return line


def run(a):
    tmp = tempfile.mkdtemp(prefix='tm_m3_')
    user = os.path.join(tmp, 'user')
    trace = os.path.join(tmp, 'imports.json')
    m = {'errors': []}
    game_before = snapshot(a.game)
    try:
        os.mkdir(user)
        os.mkdir(os.path.join(user, 'My Documents'))
        env = dict(os.environ, HLE_FILETRACE='1', SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        system = os.path.join(tmp, 'system')       # the runtime Windows directory
        cmd = [a.host, a.game, '--watchdog', str(a.timeout), '--user', user, '--system', system,
               '--import-trace', trace]
        try:
            p = subprocess.run(cmd, cwd=a.game, env=env, capture_output=True, text=True, errors='replace',
                               timeout=a.timeout + 30)
            out, code = p.stdout + p.stderr, p.returncode
        except subprocess.TimeoutExpired as e:
            out, code = str(e.stdout or '') + str(e.stderr or ''), 'timeout'
        m['user_after'] = listing(user)
        m['system_after'] = listing(system) if os.path.isdir(system) else None
        if os.path.exists(trace):
            m['trace'] = json.load(open(trace))
            if a.trace_out:
                shutil.copyfile(trace, a.trace_out)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if snapshot(a.game) != game_before:
        changed = sorted(set(snapshot(a.game).items()) ^ set(game_before.items()))
        m['errors'].append('the game directory changed: %s' % [c[0] for c in changed][:10])
    lines = out.splitlines()
    m['exit'] = code
    m['bad'] = [n for n, rx in BAD if rx.search(out)]
    failed = [x.group(1).lower() for x in map(DELAY_DLL.match, lines) if x]
    if 'delay-load failure' in m['bad'] and failed and all(d in BOUNDARY_DLLS for d in failed):
        m['bad'].remove('delay-load failure')               # the planned boundary
        m['boundary_dlls'] = failed
    m['dinput8'] = [normalise(l) for l in lines if l.startswith('[dinput8] thread')]
    m['dshow'] = [normalise(l) for l in lines if l.startswith('[dshow] ')]
    m['host_dlls'] = [l for l in lines if l.startswith('[loader] LoadLibraryA')]
    m['marks'] = [normalise(l) for l in lines if MARK.match(l)][-14:]
    m['imports_called'] = sorted(i['name'] for i in m.get('trace', {}).get('imports', []))
    m['log_tail'] = lines[-15:]
    return m


def compare(m, base):
    fails = list(m['errors'])
    fails += ['in the run: %s' % b for b in m['bad']]
    if not base:
        return fails + ['no baseline (create one with --update after reviewing the run)']
    if m['exit'] != base['exit']:
        fails.append('exit code %s, baseline %s' % (m['exit'], base['exit']))
    for k in ('marks', 'dinput8', 'dshow', 'host_dlls'):
        if k in base and m[k] != base[k]:
            fails.append('%s differ:\n    got      %s\n    baseline %s' % (k, '\n             '.join(m[k]),
                                                                     '\n             '.join(base[k])))
    new = sorted(set(m['imports_called']) - set(base['imports_called']))
    gone = sorted(set(base['imports_called']) - set(m['imports_called']))
    if new or gone:
        fails.append('imports called: new %s, no longer called %s' % (new, gone))
    if m['user_after'] != base['user_after']:
        fails.append('user directory after the run: %s, baseline %s' % (m['user_after'], base['user_after']))
    return fails


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--trace-out', help='keep the run\'s import trace (JSON) here')
    ap.add_argument('--update', action='store_true', help='write the baseline from this run')
    a = ap.parse_args(argv)
    m = run(a)
    print('[m3_boundary] exit %s; stop:\n  %s' % (m['exit'], '\n  '.join(m['marks'][-4:])))
    base = json.load(open(BASELINE)) if os.path.exists(BASELINE) else None
    if a.update:
        if m['errors'] or m['bad'] or not m['marks']:
            print('  refusing --update: %s' % (m['errors'] + m['bad'] or ['no boundary markers']))
            return 1
        keep = {k: m[k] for k in ('exit', 'marks', 'dinput8', 'dshow', 'host_dlls', 'imports_called', 'user_after')}
        json.dump(keep, open(BASELINE, 'w'), indent=1, ensure_ascii=False)
        print('  baseline written: %s' % os.path.relpath(BASELINE, REPO))
        return 0
    fails = compare(m, base)
    for f in fails:
        print('  FAIL', f)
    if fails:
        print('  --- last lines of the run ---\n  ' + '\n  '.join(m['log_tail']))
        return 1
    print('  PASS')
    return 0


if __name__ == '__main__':
    sys.exit(main())
