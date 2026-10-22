"""Stage the unmodified kernel under QEMU for original-class ABI observations.

Adapted from xVanTuring/UniXNU-qemu, BSD-2-Clause-Patent. The phone payload is
deliberately not used: the guest needs QEMU's GIC, memory map and one CPU.
There is no phone storage, private platform seed, Data volume or GPU addon.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT.parent


def stage(esp, kernel, arguments=''):
    unixnu = WORK / 'package/Build/Out/AARCH64-DEBUG/UniXNU.efi'
    if not unixnu.is_file():
        raise SystemExit(f'{unixnu} missing (unixnu build package)')
    darwinboot = ROOT / 'build/darwinboot-efi/DarwinBoot.efi'
    if not darwinboot.is_file():
        subprocess.run([sys.executable, ROOT / 'scripts/build-darwinboot-efi.py'], check=True)
    shutil.rmtree(esp, ignore_errors=True)
    payloads = esp / 'EFI/UniXNU/Payloads'
    payloads.mkdir(parents=True)
    boot = esp / 'EFI/BOOT'
    boot.mkdir()
    shutil.copyfile(unixnu, boot / 'BOOTAA64.EFI')
    shutil.copyfile(darwinboot, payloads / 'DarwinBoot.efi')
    shutil.copyfile(kernel, payloads / 'kernel.macho')
    subprocess.run([sys.executable, ROOT / 'scripts/make-qemu-adt.py', '--output', str(payloads / 'kernel.macho.adt'),
                    '--dram-size', '0x200000000', '--owned-memory-size', '0x100000000', '--cpu-regs', '0',
                    '--ipad-identity', '--unique-chip-id', '1', '--chip-id', '51454d55'], check=True)
    args = arguments + ' -v serial=3 debug=0x14e kt_fixed_zones=120 sheng-pmgr=2 -no_slto_panic ml-timeout-lock-panic-scale=40'
    (esp / 'EFI/UniXNU/unixnu.toml').write_text(
        'version = 1\n[boot]\ntimeout = 0\ndefault = "xnu"\n[kits]\nautoload = false\n'
        '[[driver]]\npath = "Payloads/DarwinBoot.efi"\n'
        '[[entry]]\nid = "xnu"\ntitle = "Original ABI observation in QEMU"\ntype = "xnu"\n'
        'path = "Payloads/kernel.macho"\n' + f'args = {json.dumps(args)}\n')
