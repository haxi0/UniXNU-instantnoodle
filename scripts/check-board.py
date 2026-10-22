#!/usr/bin/env python3
"""Host tests of board decoding and the generated eight-core Apple device tree."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import tomllib

ROOT = Path(__file__).resolve().parents[1]


def read_node(blob, offset=0):
    properties, children = struct.unpack_from('<II', blob, offset)
    offset += 8
    result = {}
    for _ in range(properties):
        name, size = struct.unpack_from('<32sI', blob, offset)
        offset += 36
        result[name.rstrip(b'\0').decode()] = blob[offset:offset + size]
        offset += (size + 3) & ~3
    nodes = []
    for _ in range(children):
        child, offset = read_node(blob, offset)
        nodes.append(child)
    result['children'] = {child['name'].rstrip(b'\0').decode(): child for child in nodes}
    return result, offset


def main():
    board = tomllib.loads((ROOT / 'board.toml').read_text())
    assert 'sheng-smp=1' in board['payload']['kernel_args'], 'eight-core ADT requires ArmCpu secondary startup'
    with tempfile.TemporaryDirectory(prefix='instantnoodle-board-') as directory:
        work = Path(directory)
        binary = work / 'BoardTests'
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                        str(ROOT / 'package/BoardTests.c'), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        output = work / 'board.adt'
        generate = [sys.executable, str(ROOT / board['payload']['adt']['tool']), '--output', str(output),
                    '--dram-size', board['payload']['dram_size'], '--cpu-regs', board['payload']['adt']['cpu_regs'],
                    '--owned-memory-size', board['payload']['adt']['owned_memory_size'], '--ipad-identity',
                    '--packed-data-ramdisk', '--trust-cache']
        subprocess.run(generate, check=True)
        blob = output.read_bytes()
        tree, end = read_node(blob)
        assert end == len(blob)
        children = tree['children']
        cpus = children['cpus']['children']
        assert len(cpus) == 8
        for i, cpu in enumerate(cpus.values()):
            assert struct.unpack('<I', cpu['reg'])[0] == i << 8
            assert cpu['compatible'] == (b'arm,cortex-a55\0' if i < 4 else b'arm,cortex-a77\0')
            assert struct.unpack('<I', cpu['timebase-frequency'])[0] == 19200000
        assert children['psci']['method'] == b'smc\0'
        assert struct.unpack('<I', children['defaults']['kern.maxvnodes'])[0] == 66560
        intc = children['arm-io']['children']['intc']
        assert struct.unpack('<4Q', intc['reg']) == (0xa00000, 0x10000, 0xa60000, 0x100000)
        assert struct.unpack('<Q', intc['redistributor-stride'])[0] == 0x20000
        assert children['arm-io']['children']['ufshc']['compatible'] == b'qcom,sm8250-ufshc\0'
        assert children['chosen']['children']['memory-map']['PackedRAMDisk'] == bytes(16)
        assert struct.unpack('<Q', children['chosen']['dram-size'])[0] == 0x200000000
        # Invalid CPU affinities must fail instead of silently describing a different CPU topology.
        for value in ('0,0', '0,0x800', '0x100,0', '0,1'):
            bad = subprocess.run(generate + ['--cpu-regs', value], capture_output=True)
            assert bad.returncode != 0, value
    print('PASS: eight-core ADT, PSCI/GIC/storage identity and invalid topology refusal')


if __name__ == '__main__':
    main()
