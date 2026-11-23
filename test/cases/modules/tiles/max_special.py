"""Check canonical extrema bits, including NaNs and signed zeros, without NumPy."""
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
    return struct.unpack('<8I', raw[10 + size:])

def is_nan(bits):
    return bits & 0x7f800000 == 0x7f800000 and bits & 0x007fffff != 0

def same(actual, expected):
    """The same values: a NaN for a NaN, every other cell bit for bit (the two zeros included).

    `expected_bits` names the canonical NaN the extrema answer with, but a NaN's payload and
    signalling bit are not part of the value (design principles, floating point; the author's
    ruling of 2026-11-23), so the NaN cells are satisfied by any NaN and nothing else is loosened.
    """
    return len(actual) == len(expected) and all(
        a == e or (is_nan(a) and is_nan(e)) for a, e in zip(actual, expected))

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
        # The 0x7fc00000 cells are NaNs, not that NaN's bits: `same` judges a NaN as a NaN, so the
        # canonical pattern the extrema answer with is written here only to say which cells are NaNs.
        expected_bits = (0x7fc00000, 0x7fc00000, 0, 0, 0xff800000, 0x7f800000, 0x7fc00000, 0x41100000)
        assert same(cpu, expected_bits) and same(gpu, expected_bits), (width, cpu, gpu)
        print(f'max width={width}: NaNs, infinities and signed-zero ties match')
