#!/usr/bin/env python3
"""Install The Movies from the user's own disc images, without Windows.

Reads the Windows Installer database (`The Movies(TM).msi`) on disc 1 and does
what msiexec's InstallFiles would do for the game's files: every row of the
File table is placed at the path the Directory table gives it, taken either
from its cabinet on the right disc (msidbFileAttributesCompressed) or loose
from the disc's source tree. Nothing is executed: no InstallShield scripts,
no custom actions, no SafeDisc driver.

    python3 tools/tm_install.py --iso DIR --out game/

DIR holds MOVIES_1.iso, MOVIES_2.iso and MOVIES_3.iso (matched by volume
label, not by file name). Output:

    game/                   the install, laid out as under INSTALLDIR
    game/_outside/<DIR>/    the few files the MSI puts outside INSTALLDIR
    game/tm_manifest.json   every file: path, size, sha256, where it came from,
                            and whether its size matches the File table

Needs: Python 3.10+, pycdlib, and on PATH msiinfo (msitools) and cabextract.
"""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

MSI_NAME = 'the movies(tm).msi'
ATTR_NONCOMPRESSED = 0x2000
ATTR_COMPRESSED = 0x4000


# --------------------------------------------------------------------------
# MSI tables (through msiinfo, which reads the compound file for us)

def msi_table(msi_path, table):
    """Rows of one MSI table as dicts. msiinfo export prints three header
    lines (column names, column types, table name + key) then tab-separated
    rows."""
    out = subprocess.run(['msiinfo', 'export', msi_path, table], check=True,
                         capture_output=True).stdout.decode('utf-8', 'replace')
    lines = out.split('\r\n') if '\r\n' in out else out.split('\n')
    cols = lines[0].split('\t')
    rows = []
    for ln in lines[3:]:
        if not ln:
            continue
        vals = ln.split('\t')
        vals += [''] * (len(cols) - len(vals))
        rows.append(dict(zip(cols, vals)))
    return rows


def long_name(part):
    """'SHORT~1|Long Name' -> 'Long Name'; 'Name' -> 'Name'."""
    return part.split('|')[-1]


def split_default_dir(default_dir):
    """DefaultDir is 'target' or 'target:source' (each optionally short|long).
    Returns (target, source) long names; '.' means 'the parent itself'."""
    if ':' in default_dir:
        tgt, src = default_dir.split(':', 1)
    else:
        tgt = src = default_dir
    return long_name(tgt), long_name(src)


def resolve_dirs(directory_rows, install_dir='INSTALLDIR'):
    """For every Directory key: (target path relative to INSTALLDIR or None if
    outside it, outside-root key, source path relative to the disc root).

    Target paths walk up to INSTALLDIR; a directory that never reaches it is
    'outside' and is reported under the topmost directory below TARGETDIR.
    Source paths walk up to TARGETDIR (SourceDir = the disc root), the way
    Windows Installer finds uncompressed files on the source medium."""
    by_key = {r['Directory']: r for r in directory_rows}
    res = {}

    def chain(key):
        seen, out = set(), []
        while key and key in by_key and key not in seen:
            seen.add(key)
            out.append(key)
            key = by_key[key]['Directory_Parent']
        return out          # key ... TARGETDIR

    for key in by_key:
        keys = chain(key)
        # source: from the root down, skipping '.' and TARGETDIR itself
        src_parts = []
        for k in reversed(keys):
            if not by_key[k]['Directory_Parent']:
                continue
            _, s = split_default_dir(by_key[k]['DefaultDir'])
            if s != '.':
                src_parts.append(s)
        # target: relative to INSTALLDIR if it is an ancestor-or-self
        if install_dir in keys:
            below = keys[:keys.index(install_dir)]
            tgt_parts = []
            for k in reversed(below):
                t, _ = split_default_dir(by_key[k]['DefaultDir'])
                if t != '.':
                    tgt_parts.append(t)
            res[key] = ('/'.join(tgt_parts), None, '/'.join(src_parts))
        else:
            top = keys[-2] if len(keys) >= 2 else key
            tgt_parts = []
            for k in reversed(keys[:-1]):
                t, _ = split_default_dir(by_key[k]['DefaultDir'])
                if t != '.':
                    tgt_parts.append(t)
            res[key] = ('/'.join(tgt_parts), top, '/'.join(src_parts))
    return res


def media_for_sequence(media_rows, seq):
    """The Media row whose LastSequence is the first at or above seq."""
    for m in sorted(media_rows, key=lambda r: int(r['LastSequence'])):
        if seq <= int(m['LastSequence']):
            return m
    raise ValueError('no Media row covers sequence %d' % seq)


# --------------------------------------------------------------------------
# Disc images

class Disc:
    def __init__(self, path):
        import pycdlib
        self.path = path
        self.iso = pycdlib.PyCdlib()
        self.iso.open(path)
        self.label = self.iso.pvds[0].volume_identifier.decode('ascii', 'replace').strip()
        self.joliet = self.iso.has_joliet()
        self.fac = self.iso.get_joliet_facade() if self.joliet else self.iso.get_iso9660_facade()
        self.index = {}             # lower-case path without ';1' -> real path
        for root, _dirs, files in self.fac.walk('/'):
            for f in files:
                real = root.rstrip('/') + '/' + f
                self.index[self._norm(real)] = real

    @staticmethod
    def _norm(p):
        p = p.replace('\\', '/').lower()
        if p.endswith(';1'):
            p = p[:-2]
        return '/' + p.strip('/')

    def find(self, rel):
        return self.index.get(self._norm(rel))

    def extract(self, real, dest):
        with open(dest, 'wb') as fp:
            if self.joliet:
                self.fac.get_file_from_iso_fp(fp, joliet_path=real)
            else:
                self.fac.get_file_from_iso_fp(fp, iso_path=real)


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, 'rb') as fp:
        for blk in iter(lambda: fp.read(1 << 20), b''):
            h.update(blk)
    return h.hexdigest()


# --------------------------------------------------------------------------

def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('--iso', required=True, help='directory holding the three disc images')
    ap.add_argument('--out', required=True, help='install directory (created)')
    ap.add_argument('--no-hash', action='store_true', help='skip sha256 in the manifest')
    args = ap.parse_args(argv)

    for tool in ('msiinfo', 'cabextract'):
        if not shutil.which(tool):
            sys.exit('%s not found on PATH (apt install msitools cabextract)' % tool)

    discs = {}
    for fn in sorted(os.listdir(args.iso)):
        if fn.lower().endswith('.iso'):
            d = Disc(os.path.join(args.iso, fn))
            discs[d.label.upper()] = d
            print('[disc] %-12s %s' % (d.label, fn))
    disc1 = next((d for d in discs.values() if d.find(MSI_NAME)), None)
    if not disc1:
        sys.exit('no disc holds %s' % MSI_NAME)

    os.makedirs(args.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix='tm_install_', dir=args.out)
    try:
        msi = os.path.join(work, 'game.msi')
        disc1.extract(disc1.find(MSI_NAME), msi)
        files = msi_table(msi, 'File')
        media = msi_table(msi, 'Media')
        comps = {r['Component']: r for r in msi_table(msi, 'Component')}
        dirs = resolve_dirs(msi_table(msi, 'Directory'))
        print('[msi] %d files, %d media, %d components, %d directories'
              % (len(files), len(media), len(comps), len(dirs)))

        # Group compressed files by cabinet so each cabinet is read once.
        by_cab, loose = {}, []
        for f in files:
            seq = int(f['Sequence'])
            m = media_for_sequence(media, seq)
            attrs = int(f['Attributes'] or 0)
            if m['Cabinet'] and not (attrs & ATTR_NONCOMPRESSED):
                by_cab.setdefault((m['VolumeLabel'].upper(), m['Cabinet']), []).append(f)
            else:
                loose.append((m, f))

        manifest, problems = [], []

        def place(f, src_path, origin):
            tgt_rel, outside, _ = dirs[comps[f['Component_']]['Directory_']]
            name = long_name(f['FileName'])
            rel = '/'.join(p for p in (tgt_rel, name) if p)
            if outside:
                rel = '_outside/%s/%s' % (outside, rel)
            dest = os.path.join(args.out, rel)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            shutil.move(src_path, dest)
            size = os.path.getsize(dest)
            want = int(f['FileSize'])
            ent = {'path': rel, 'size': size, 'msi_size': want, 'size_ok': size == want,
                   'origin': origin, 'msi_version': f.get('Version') or None}
            if not args.no_hash:
                ent['sha256'] = sha256_file(dest)
            manifest.append(ent)
            if size != want:
                problems.append('%s: %d bytes on disc, File table says %d' % (rel, size, want))

        for (label, cab), members in sorted(by_cab.items()):
            disc = discs.get(label)
            real = disc and disc.find(cab)
            if not real:
                problems.append('cabinet %s not found on %s' % (cab, label))
                continue
            cab_local = os.path.join(work, 'cab.cab')
            disc.extract(real, cab_local)
            out_dir = os.path.join(work, 'cab')
            os.makedirs(out_dir, exist_ok=True)
            r = subprocess.run(['cabextract', '-q', '-d', out_dir, cab_local],
                               capture_output=True, text=True)
            if r.returncode != 0:
                problems.append('cabextract %s: %s' % (cab, r.stderr.strip()))
            got = {fn.lower(): fn for fn in os.listdir(out_dir)}
            for f in members:
                key = f['File'].lower()
                if key not in got:
                    problems.append('%s: member %s missing from %s' % (long_name(f['FileName']), f['File'], cab))
                    continue
                place(f, os.path.join(out_dir, got[key]), '%s:%s' % (label, cab))
            shutil.rmtree(out_dir)
            os.remove(cab_local)
            print('[cab]  %-14s %-10s %4d files' % (cab, label, len(members)))

        for m, f in loose:
            disc = discs.get(m['VolumeLabel'].upper())
            _, _, src_rel = dirs[comps[f['Component_']]['Directory_']]
            real = None
            if disc:
                for nm in (long_name(f['FileName']), f['FileName'].split('|')[0]):
                    real = disc.find('/'.join(p for p in (src_rel, nm) if p))
                    if real:
                        break
            if not real:
                problems.append('%s: not on %s under %s' % (long_name(f['FileName']), m['VolumeLabel'], src_rel))
                continue
            tmp = os.path.join(work, 'loose.bin')
            disc.extract(real, tmp)
            place(f, tmp, '%s:%s' % (m['VolumeLabel'].upper(), real))
        print('[loose] %d files' % len(loose))
    finally:
        shutil.rmtree(work, ignore_errors=True)

    manifest.sort(key=lambda e: e['path'].lower())
    total = sum(e['size'] for e in manifest)
    with open(os.path.join(args.out, 'tm_manifest.json'), 'w') as fp:
        json.dump({'files': manifest, 'problems': problems, 'total_bytes': total}, fp, indent=1)
    print('[done] %d of %d files, %.2f GB, %d problems'
          % (len(manifest), len(files), total / 1e9, len(problems)))
    for p in problems:
        print('  !', p)
    return 0 if len(manifest) == len(files) else 1


if __name__ == '__main__':
    sys.exit(main())
