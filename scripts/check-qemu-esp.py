#!/usr/bin/env python3
"""Check QEMU staging in an assembled tree after building the EFI package and DarwinBoot."""
from pathlib import Path
import struct
import sys
import tempfile
import tomllib

if len(sys.argv) != 2:
    raise SystemExit('usage: check-qemu-esp.py ASSEMBLED_TREE')
tree = Path(sys.argv[1]).resolve()
sys.path.insert(0, str(tree / 'scripts'))
from qemu_esp import stage
from importlib.util import module_from_spec, spec_from_file_location
spec = spec_from_file_location('qemu_adt', tree / 'scripts/make-qemu-adt.py')
adt = module_from_spec(spec)
spec.loader.exec_module(adt)
with tempfile.TemporaryDirectory(prefix='instantnoodle-qemu-stage-') as directory:
    work = Path(directory)
    kernel = work / 'kernel.macho'
    kernel.write_bytes(b'kernel staging marker\0')
    esp = work / 'esp'
    stage(esp, kernel, 'offline-apfs-load=1')
    payload = esp / 'EFI/UniXNU/Payloads'
    assert (payload / 'kernel.macho').read_bytes() == kernel.read_bytes()
    assert {path.name for path in payload.iterdir()} == {'kernel.macho', 'kernel.macho.adt', 'DarwinBoot.efi'}
    for image in (payload / 'DarwinBoot.efi', esp / 'EFI/BOOT/BOOTAA64.EFI'):
        assert image.read_bytes()[:2] == b'MZ'
    config = tomllib.loads((esp / 'EFI/UniXNU/unixnu.toml').read_text())
    assert config['driver'] == [{'path': 'Payloads/DarwinBoot.efi'}]
    assert config['entry'][0]['args'].startswith('offline-apfs-load=1 ')
    assert 'instantnoodle-' not in config['entry'][0]['args']
    blob = (payload / 'kernel.macho.adt').read_bytes()
    node, end = adt.parse(blob)
    assert end == len(blob)
    assert len(adt.child(node, 'cpus')[2]) == 1
    assert not {'ufshc', 'ufsphy'} & {child[0] for child in adt.child(node, 'arm-io')[2]}
    chosen = dict(adt.child(node, 'chosen')[1])
    assert chosen['unique-chip-id'] == struct.pack('<Q', 1)
    assert chosen['chip-id'] == struct.pack('<I', 0x51454d55)
    assert chosen['dram-size'] == struct.pack('<Q', 0x200000000)
print('PASS: unmodified kernel, real ARM64 EFI images, isolated one-core QEMU ADT and test identity')
