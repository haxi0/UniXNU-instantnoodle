#!/usr/bin/env python3
"""Observe original storage/display classes in QEMU and export their audit evidence.

Run from the assembled tree after two kernel passes and unixnu build package.
QEMU boots the unmodified candidate without phone storage or a Data volume.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
for name in ('build-storage-abi.py', 'build-display-abi.py', 'audit-mobile-graphics-legacy.py'):
    subprocess.run([sys.executable, ROOT / 'scripts' / name], check=True)
out = ROOT.parent / 'kernel/abi'
out.mkdir(exist_ok=True)
for source, name in (
    ('storage-abi/interface-audit.json', 'storage-audit.json'),
    ('darwinboot-storage-native-metadata/gdb-stop.json', 'storage-observation.json'),
    ('display-analysis/mobile-graphics-legacy-abi.json', 'display-audit.json'),
    ('darwinboot-owner-hvf-mobile-graphics-legacy-class/gdb-stop.json', 'display-observation.json'),
):
    source = ROOT / 'build' / source
    json.loads(source.read_text())
    shutil.copyfile(source, out / name)
print(f'Qualified storage/display ABI evidence: {out}')
