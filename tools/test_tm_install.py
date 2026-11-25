#!/usr/bin/env python3
"""Unit tests for tm_install's MSI logic, on synthetic tables (no game data).

    python3 -m unittest tools/test_tm_install.py
"""
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from tm_install import long_name, media_for_sequence, resolve_dirs, split_default_dir  # noqa: E402


def d(key, parent, default):
    return {'Directory': key, 'Directory_Parent': parent, 'DefaultDir': default}


# The shape of the Russian CD's Directory table: TARGETDIR = SourceDir (the
# disc root), ProgramFilesFolder whose source is Setup, an Activision folder
# whose source is Data, INSTALLDIR '.', then Data/pak.
DIRS = [
    d('TARGETDIR', '', 'SourceDir'),
    d('ProgramFilesFolder', 'TARGETDIR', '.:SETUP|Setup'),
    d('ACTIVISION', 'ProgramFilesFolder', 'DATA|Data'),
    d('INSTALLDIR', 'ACTIVISION', '.'),
    d('DATA', 'INSTALLDIR', 'Data'),
    d('PAK', 'DATA', 'pak'),
    d('STARMAKER', 'DATA', 'STARMA~1|StarMaker'),
    d('ROOT', 'TARGETDIR', 'Root:.'),
    d('DOCS', 'TARGETDIR', 'Docs:Docs'),
    d('RUS', 'DOCS', '.:RUS'),
]


class Names(unittest.TestCase):
    def test_long_name(self):
        self.assertEqual(long_name('TEX_AL~1.PAK|TEX_ALL0000.pak'), 'TEX_ALL0000.pak')
        self.assertEqual(long_name('Movies.exe'), 'Movies.exe')

    def test_split_default_dir(self):
        self.assertEqual(split_default_dir('.:SETUP|Setup'), ('.', 'Setup'))
        self.assertEqual(split_default_dir('STARMA~1|StarMaker'), ('StarMaker', 'StarMaker'))
        self.assertEqual(split_default_dir('Root:.'), ('Root', '.'))


class Dirs(unittest.TestCase):
    def setUp(self):
        self.r = resolve_dirs(DIRS)

    def test_install_dir_is_root(self):
        self.assertEqual(self.r['INSTALLDIR'], ('', None, 'Setup/Data'))

    def test_data_tree(self):
        # target under INSTALLDIR; source is where the disc keeps it
        self.assertEqual(self.r['PAK'], ('Data/pak', None, 'Setup/Data/Data/pak'))
        self.assertEqual(self.r['STARMAKER'], ('Data/StarMaker', None, 'Setup/Data/Data/StarMaker'))

    def test_outside_install_dir(self):
        tgt, outside, src = self.r['RUS']
        self.assertEqual(outside, 'DOCS')
        self.assertEqual(tgt, 'Docs')          # RUS's target is '.'
        self.assertEqual(src, 'Docs/RUS')
        self.assertEqual(self.r['ROOT'], ('Root', 'ROOT', ''))


class Media(unittest.TestCase):
    MEDIA = [{'DiskId': '1', 'LastSequence': '106', 'Cabinet': 'a.cab'},
             {'DiskId': '3', 'LastSequence': '370', 'Cabinet': ''},
             {'DiskId': '2', 'LastSequence': '112', 'Cabinet': 'b.cab'}]

    def test_ranges(self):
        self.assertEqual(media_for_sequence(self.MEDIA, 1)['DiskId'], '1')
        self.assertEqual(media_for_sequence(self.MEDIA, 106)['DiskId'], '1')
        self.assertEqual(media_for_sequence(self.MEDIA, 107)['DiskId'], '2')
        self.assertEqual(media_for_sequence(self.MEDIA, 366)['DiskId'], '3')
        with self.assertRaises(ValueError):
            media_for_sequence(self.MEDIA, 371)


if __name__ == '__main__':
    unittest.main()
