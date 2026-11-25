#!/usr/bin/env python3
r"""M2 regression test: the lifted CRT of Movies.exe 1.1 still reaches WinMain.

    python3 tests/m2_winmain.py --host ~/tm/build/tm_host --game ~/tm/game
    python3 tests/m2_winmain.py ... --gen ~/tm/work/gen
    python3 tests/m2_winmain.py ... --scenario empty_user_dir_with_documents
    python3 tests/m2_winmain.py ... --scenario NAME --update     # accept a reviewed baseline

Each scenario sets up the guest's user directory (D:)
explicitly, checks that state before the run, runs
`tm_host <game> --stop-at-winmain` with the game directory as the working
directory, removes the user directory and checks the removal.

The working directory is part of the scenario: the guest's current directory
is the host's, mapped through the drives (C:\ in the game directory, Z:\...
anywhere else), and the CRT start-up code before WinMain takes a different
path for each (a Windows shortcut starts the game in its install directory,
so that is the one used here). The game directory is snapshotted before and after: any change
fails the test.

Compared with the scenario's baseline (tests/baselines/m2_<scenario>.json):

  * exit code 0 and the `[poc] WinMain 0x005303C0 entered` line;
  * no `ICALL: unresolved`, `ITAIL`, `[not-lifted]`, `FATAL`, `===` crash
    report or `[watchdog]` line before WinMain;
  * function entries and distinct functions before WinMain, exactly;
  * imports per status (docs/IMPORTS.md): unsupported may not grow, and no
    shim in src/host may be without a status;
  * with --gen: 0 lift errors, 0 not-lifted stubs, unresolved ITAILs not
    above the baseline.

A baseline is written only by --update, for one named scenario, and only
from a clean run. Exit 0 if every scenario run passes, 1 otherwise.
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
BASELINES = os.path.join(HERE, 'baselines')


def setup_with_documents(root):
    os.mkdir(os.path.join(root, 'My Documents'))


def setup_without_documents(root):
    pass


# name -> (set-up function, expected listing of the user directory before the run, role)
SCENARIOS = {
    'empty_user_dir_with_documents': (setup_with_documents, ['My Documents/'], 'main'),
    'empty_user_dir_without_documents': (setup_without_documents, [], 'diagnostic'),
}

WINMAIN_RE = re.compile(r'\[poc\] WinMain 0x005303C0 entered: [\d.]+ ms, (\d+) function entries, (\d+) distinct functions')
STATUSES = ('implemented', 'diagnostic', 'win32hle-present', 'win32hle-verified', 'unsupported')
IMPORTS_RE = re.compile(r'\[imports\] (\d+) bound: ' + ', '.join(r'(\d+) ' + re.escape(s) for s in STATUSES))
BAD = [('unresolved indirect call', re.compile(r'ICALL: unresolved')),
       ('unresolved jump', re.compile(r'ITAIL')),
       ('function not lifted', re.compile(r'\[not-lifted\]')),
       ('win32hle fatal', re.compile(r'FATAL')),
       ('crash report', re.compile(r'^=== ', re.M)),
       ('watchdog', re.compile(r'\[watchdog\]'))]


def listing(root):
    out = []
    for d, dirs, files in os.walk(root):
        rel = os.path.relpath(d, root)
        for x in dirs:
            out.append(os.path.normpath(os.path.join(rel, x)) + '/')
        for x in files:
            out.append(os.path.normpath(os.path.join(rel, x)))
    return sorted(out)


def snapshot(root):
    snap = {}
    for d, _dirs, files in os.walk(root):
        for x in files:
            p = os.path.join(d, x)
            st = os.stat(p)
            snap[os.path.relpath(p, root)] = (st.st_size, st.st_mtime_ns)
    return snap


def run_scenario(a, name):
    setup, expected, _role = SCENARIOS[name]
    m = {'scenario': name, 'cwd': 'game', 'setup_errors': []}
    game_before = snapshot(a.game)
    tmp = tempfile.mkdtemp(prefix='tm_m2_')
    user = os.path.join(tmp, 'user')
    imports_json = os.path.join(tmp, 'imports.json')
    try:
        os.mkdir(user)
        setup(user)
        got = listing(user)
        if got != expected:
            m['setup_errors'].append('user directory before the run is %s, expected %s' % (got, expected))
            return m
        system = os.path.join(tmp, 'system')       # the runtime Windows directory, apart from game and user
        cmd = [a.host, a.game, '--stop-at-winmain', '--watchdog', str(a.timeout),
               '--user', user, '--system', system, '--imports-json', imports_json]
        try:
            p = subprocess.run(cmd, cwd=a.game, capture_output=True, text=True, errors='replace',
                               timeout=a.timeout + 30)
            out, code = p.stdout + p.stderr, p.returncode
        except subprocess.TimeoutExpired as e:
            out, code = str(e.stdout or '') + str(e.stderr or ''), 'timeout'
        imports = json.load(open(imports_json)) if os.path.exists(imports_json) else None
        m['user_after'] = listing(user)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if os.path.exists(tmp):
        m['setup_errors'].append('scenario directory %s was not removed' % tmp)
    game_after = snapshot(a.game)
    if game_after != game_before:
        changed = sorted(set(game_after.items()) ^ set(game_before.items()))
        m['setup_errors'].append('the game directory changed: %s' % [c[0] for c in changed][:10])

    m.update({'exit': code, 'log': out})
    w = WINMAIN_RE.search(out)
    m['winmain'] = bool(w)
    if w:
        m['entries'], m['distinct'] = int(w.group(1)), int(w.group(2))
    before = out[:w.start()] if w else out
    m['bad'] = [n for n, rx in BAD if rx.search(before)]
    i = IMPORTS_RE.search(out)
    if i:
        vals = list(map(int, i.groups()))
        m['bound'] = vals[0]
        m['imports'] = dict(zip(STATUSES, vals[1:]))
    if imports:
        m['unsupported_names'] = sorted(x['name'] for x in imports['imports'] if x['status'] == 'unsupported')
    return m


def own_shims_without_status():
    """Names registered in src/host shim tables but missing from tm_imports.c."""
    src = os.path.join(REPO, 'src', 'host')
    known = set(re.findall(r'\{ "(\w+)", TM_', open(os.path.join(src, 'tm_imports.c')).read()))
    shims = set()
    for fn in os.listdir(src):
        if fn.endswith('.c') and fn != 'tm_imports.c':
            shims |= set(re.findall(r'\{ "(\w+)",\s+\w+ \}', open(os.path.join(src, fn)).read()))
    return sorted(shims - known)


def lift_health(gen):
    out = subprocess.run([sys.executable, os.path.join(REPO, 'tools', 'lift_health.py'), gen, '--json', os.devnull],
                         capture_output=True, text=True).stdout
    get = lambda k: int(re.search(k + r'\s*:\s*(\d+)', out).group(1))
    e = re.search(r'errors (\d+)', out)
    return {'lift_errors': int(e.group(1)) if e else 0, 'not_lifted': get('not-lifted stubs'),
            'unresolved_itail': get('unresolved ITAIL')}


def check(m, base, health):
    fails = list(m['setup_errors'])
    if 'exit' not in m:
        return fails
    if m['exit'] != 0:
        fails.append('exit code %s, expected 0' % m['exit'])
    if not m['winmain']:
        fails.append('WinMain not reached')
    fails += ['before WinMain: %s' % b for b in m['bad']]
    if not base:
        fails.append('no baseline for this scenario (create one with --scenario %s --update)' % m['scenario'])
        return fails
    if base.get('cwd') != m['cwd']:
        fails.append('baseline taken with working directory %r, this run uses %r' % (base.get('cwd'), m['cwd']))
    for k in ('entries', 'distinct'):
        if m.get(k) != base.get(k):
            fails.append('%s before WinMain: %s, baseline %s' % (k, m.get(k), base.get(k)))
    got_u = (m.get('imports') or {}).get('unsupported', 10 ** 9)
    if got_u > base['imports']['unsupported']:
        new = sorted(set(m.get('unsupported_names', [])) - set(base.get('unsupported_names', [])))
        fails.append('unsupported imports: %s, baseline %s (new: %s)' % (got_u, base['imports']['unsupported'], new))
    if health:
        if health['lift_errors'] or health['not_lifted']:
            fails.append('lift: %d errors, %d not-lifted stubs' % (health['lift_errors'], health['not_lifted']))
        if 'unresolved_itail' in base and health['unresolved_itail'] > base['unresolved_itail']:
            fails.append('unresolved ITAIL: %d, baseline %d' % (health['unresolved_itail'], base['unresolved_itail']))
    return fails


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument('--host', required=True)
    ap.add_argument('--game', required=True)
    ap.add_argument('--gen')
    ap.add_argument('--scenario', choices=sorted(SCENARIOS), help='one scenario (default: all)')
    ap.add_argument('--timeout', type=int, default=120)
    ap.add_argument('--update', action='store_true', help='write the baseline of --scenario from this run')
    a = ap.parse_args(argv)
    if a.update and not a.scenario:
        ap.error('--update needs --scenario: baselines are updated one scenario at a time, on purpose')

    health = lift_health(a.gen) if a.gen else None
    unstatused = own_shims_without_status()
    names = [a.scenario] if a.scenario else sorted(SCENARIOS, key=lambda n: SCENARIOS[n][2] != 'main')
    failed = False
    for name in names:
        m = run_scenario(a, name)
        path = os.path.join(BASELINES, 'm2_%s.json' % name)
        base = json.load(open(path)) if os.path.exists(path) else None
        print('[%s] (%s) exit %s, WinMain %s, %s entries, %s distinct, imports %s'
              % (name, SCENARIOS[name][2], m.get('exit'), 'reached' if m.get('winmain') else 'NOT reached',
                 m.get('entries'), m.get('distinct'), m.get('imports')))
        if a.update:
            if m['setup_errors'] or m.get('exit') != 0 or not m.get('winmain') or m.get('bad'):
                print('  refusing --update: this run is not clean')
                failed = True
                continue
            new = {k: m[k] for k in ('cwd', 'entries', 'distinct', 'bound', 'imports', 'unsupported_names') if k in m}
            if health:
                new['unresolved_itail'] = health['unresolved_itail']
            elif base and 'unresolved_itail' in base:
                new['unresolved_itail'] = base['unresolved_itail']
            os.makedirs(BASELINES, exist_ok=True)
            json.dump(new, open(path, 'w'), indent=1)
            print('  baseline written: %s' % os.path.relpath(path, REPO))
            continue
        fails = check(m, base, health)
        if unstatused:
            fails.append('shims in src/host with no status in tm_imports.c: %s' % unstatused)
        if fails:
            failed = True
            for f in fails:
                print('  FAIL', f)
            if m.get('log'):
                print('  --- last lines of the run ---\n  ' + '\n  '.join(m['log'].strip().splitlines()[-12:]))
        else:
            print('  PASS')
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
