#!/usr/bin/env python3
"""Archive the no-op probe hooks of kits/instantnoodle/LinkStubs.c for the kernel link (CONFIG_XNU_EXTRA_LD_ARCHIVES)."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / 'kits/instantnoodle/LinkStubs.c'
out = ROOT / 'build/link-stubs'
out.mkdir(parents=True, exist_ok=True)
obj, archive = out / 'LinkStubs.o', out / 'libLinkStubs.a'
subprocess.run(['xcrun', '--sdk', 'iphoneos', 'clang', '-arch', 'arm64', '-mkernel', '-miphoneos-version-min=17.0',
                '-Os', '-Wall', '-Werror', '-c', source, '-o', obj], check=True)
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', archive, obj], check=True)
# Board glue uses BSD libkern/sys headers, which conflict with MACH_KERNEL_PRIVATE declarations.
record = json.loads((ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/bsd/DEVELOPMENT/bsd_init.o.json').read_text().strip().rstrip(','))
for name in ('Platform', 'CpuFreq', 'TouchProbe', 'TouchState'):
    source = ROOT / f'kits/instantnoodle/{name}.c'
    obj = out / f'{name}.o'
    command = []
    args = record['arguments']
    i = 0
    while i < len(args):
        arg = args[i]
        if arg == record['file']:
            command.append(str(source))
        elif arg in ('-o', '-MF', '-MT', '-MJ'):
            if arg == '-o':
                command += ['-o', str(obj)]
            i += 1
        elif arg == '-Xclang' and args[i + 1].startswith('-W'):
            i += 1
        elif not arg.startswith('-W'):
            command.append(arg)
        i += 1
    command += ['-mgeneral-regs-only', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / '.cache/xnu-work/osfmk')]
    subprocess.run(command, cwd=record['directory'], check=True)
    subprocess.run(['xcrun', 'ar', 'rcs', archive, obj], check=True)
print(archive)
