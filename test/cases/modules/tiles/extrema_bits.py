"""Raw IEEE extrema payloads through generated kernels and the CPU reference."""
import ast
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
a = [0, 0x80000000, 0x7fc12345, 0xffc54321, 0x7f800001, 0x3f800000, 0xbf800000, 0xff800000]
b = [0x80000000, 0, 0xffc54321, 0x7fc12345, 0x3f800000, 0x7fa00001, 0xbf800000, 0x7f800000]
nan = 0x7fc00000
expected = {
    'hi': [0, 0, nan, nan, nan, nan, 0xbf800000, 0x7f800000],
    'lo': [0x80000000, 0x80000000, nan, nan, nan, nan, 0xbf800000, 0xff800000],
    'relu': [0, 0, nan, nan, nan, 0x3f800000, 0, 0],
    'fused': [0, 0, nan, nan, nan, nan, 0, 0x7f800000],
    'single': [0, 0x80000000, nan, nan, nan, 0x3f800000, 0xbf800000, 0xff800000],
}

def write(path, bits, shape):
    header = str(dict(descr='<f4', fortran_order=False, shape=shape))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    path.write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header)) + header.encode() + struct.pack('<' + str(len(bits)) + 'I', *bits))

def read(path):
    raw = path.read_bytes()
    assert raw[:8] == b'\x93NUMPY\x01\x00'
    size = struct.unpack_from('<H', raw, 8)[0]
    header = ast.literal_eval(raw[10:10 + size].decode())
    assert header['descr'] == '<f4' and header['shape'] == (8,)
    return list(struct.unpack('<8I', raw[10 + size:]))

with tempfile.TemporaryDirectory(prefix='camel-extrema-bits-') as folder:
    folder = Path(folder)
    env['TILES_TEST_OUT'] = str(folder)
    write(folder / 'a.npy', a, (8,))
    write(folder / 'b.npy', b, (8,))
    for vm in ['std::nvm', 'std::fvm', 'std::jit']:
        for width in [1, 127, 128, 257, 513]:
            # Put the second payload at opposite ends on adjacent rows.
            rows = []
            for row, (x, y) in enumerate(zip(a, b)):
                values = [x] * width
                if width > 1:
                    values[0 if row % 2 else -1] = y
                rows.extend(values)
            write(folder / 'rows.npy', rows, (8, width))
            result = subprocess.run([str(binary), str(Path(__file__).with_suffix('.cml')), vm], cwd=root, env=env, capture_output=True, text=True, timeout=30)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
            assert json.loads(result.stdout)['kernels'] == 1
            for name, bits in expected.items():
                for suffix in ['cpu', 'gpu'] + (['rev'] if name in ['hi', 'lo'] else []):
                    actual = read(folder / f'{name}_{suffix}.npy')
                    assert actual == bits, (vm, width, name, suffix, actual, bits)
            for suffix in ['cpu', 'gpu']:
                assert read(folder / f'rows_{suffix}.npy') == expected['single' if width == 1 else 'hi']
        print(f'{vm}: canonical extrema payloads, zeros, singleton and parallel reductions passed')
