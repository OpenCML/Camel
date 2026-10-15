"""Check selection semantics, including NaNs and signed-zero ties, without NumPy."""
import ast
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
case = Path(__file__).with_suffix('.cml')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')

def read_values(path):
    raw = path.read_bytes()
    assert raw[:8] == b'\x93NUMPY\x01\x00', 'expected npy v1'
    size = struct.unpack_from('<H', raw, 8)[0]
    header = ast.literal_eval(raw[10:10 + size].decode())
    assert header['descr'] == '<f4' and header['shape'] == (8,), header
    return struct.unpack('<8f', raw[10 + size:])

with tempfile.TemporaryDirectory(prefix='camel-tiles-max-') as folder:
    env['TILES_TEST_OUT'] = folder
    for width in [127, 128, 257, 513]:
        env['TILES_TEST_WIDTH'] = str(width)
        result = subprocess.run([str(binary), str(case)], env=env, cwd=root,
                                capture_output=True, text=True, timeout=30)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        plan = json.loads(result.stdout)
        expected = 'parallel_max' if width >= 128 else 'standard'
        assert plan['stages'][0]['implementation'] == expected, plan
        cpu, gpu = [read_values(Path(folder) / f'{name}.npy') for name in ['cpu', 'gpu']]
        for a, b in zip(cpu, gpu):
            assert (math.isnan(a) and math.isnan(b)) or struct.pack('<f', a) == struct.pack('<f', b), (width, cpu, gpu)
        assert math.isnan(gpu[0]) and math.isnan(gpu[6]), gpu
        assert gpu[1] == 4 and gpu[4] == -math.inf and gpu[5] == math.inf and gpu[7] == 9, gpu
        assert struct.pack('<f', gpu[2]) == b'\0\0\0\0', gpu
        assert struct.pack('<f', gpu[3]) == b'\0\0\0\x80', gpu
        print(f'max width={width}: NaNs, infinities and signed-zero ties match')
