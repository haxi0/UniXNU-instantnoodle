#!/usr/bin/env python3
"""Exercise the staged storage port on a host; never opens a phone or block device."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

from ufs_sm8250 import stage_sm8250_ufs

DEVICE = Path(__file__).resolve().parents[1]


def main():
    if len(sys.argv) != 2:
        raise SystemExit('usage: check-storage.py PATH_TO_UPSTREAM_NATIVEUFS')
    source = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix='instantnoodle-ufs-') as directory:
        work = Path(directory)
        staged = stage_sm8250_ufs(source, DEVICE / 'package/NativeUfs-sm8250.patch', work / 'source')
        compiler = os.environ.get('CXX', 'c++')
        for test in ('SmmuTakeover', 'RootPublication', 'TransferPoll'):
            output = work / test
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            str(staged / f'tests/{test}.cpp'), '-o', str(output)], check=True)
            subprocess.run([str(output)], check=True)
        fixture = work / 'constants.cpp'
        for variant, axi, sid, mask, memphy in ((0, 0x77018, 0x60, 0, 0x9e000), (1, 0x77010, 0xe0, 0x400, 0)):
            fixture.write_text('#include "GccUfs.h"\n'
                               f'static_assert(GCC_UFS_PHY_AXI_ENABLE == {axi});\n'
                               f'static_assert(UFS_SMMU_STREAM == {mask << 16 | sid});\n'
                               f'static_assert(GCC_UFS_MEM_PHY_GDSCR == {memphy});\n')
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                            f'-DNATIVE_UFS_SM8250={variant}', '-I', str(staged), str(fixture)], check=True)
        # A changed upstream interface must fail before patching or overwriting an existing build.
        header = staged / 'GccUfs.h'
        before = header.read_bytes()
        try:
            stage_sm8250_ufs(staged, DEVICE / 'package/NativeUfs-sm8250.patch', work / 'refused')
        except SystemExit:
            assert header.read_bytes() == before and not (work / 'refused').exists()
        else:
            raise AssertionError('modified upstream source was accepted')
    print('PASS: SM8250 patch staging, storage regressions, both SoC constants and changed-base refusal')


if __name__ == '__main__':
    main()
