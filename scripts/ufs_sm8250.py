"""Stage the device's SM8250 change without editing UniXNU's NativeUfs submodule."""
import hashlib
import shutil
import subprocess
from pathlib import Path

# UniXNU/NativeUfs af785c1777161a62931425c2faaa536ce0bebd5b.
BASE_HASHES = {
    'GccUfs.h': '1ef7030533184ecb7efebb8ad3a6fa8e155a8b5b9eea19a6a2af758027d483c3',
    'NativeUfs.cpp': '24f549be5006ab5c463c67aa01ee38511b185853f878e12b4bb70830313ae9ed',
    'SmmuTakeover.h': '54868bf0745ee6093627e73ba8cec9ac3c574446955c359463507d0f122f90a8',
    'tests/SmmuTakeover.cpp': 'd120613396e06d55a5d7c635b06b8bc8b2afe920748ca6e85d18c0b6f754d318',
}


def stage_sm8250_ufs(source, patch, destination):
    source, patch, destination = Path(source), Path(patch), Path(destination)
    for name, wanted in BASE_HASHES.items():
        if hashlib.sha256((source / name).read_bytes()).hexdigest() != wanted:
            raise SystemExit(f'NativeUfs {name} differs from the pinned SM8250 patch base; review the port before rebuilding')
    if destination.exists():
        shutil.rmtree(destination)
    shutil.copytree(source, destination, ignore=shutil.ignore_patterns('.git', '__pycache__'))
    subprocess.run(['patch', '-t', '-p1', '-F', '0', '-i', str(patch.resolve())], cwd=destination, check=True)
    return destination
