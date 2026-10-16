"""Strict row sums: exact tree guards and canonical fallback compared as IEEE bits."""
import ast
import json
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile
root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
def write(path, bits, shape):
    header = str(dict(descr='<f4', fortran_order=False, shape=shape))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    path.write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header)) + header.encode() + struct.pack('<' + str(len(bits)) + 'I', *bits))
def read(path):
    raw = path.read_bytes()
    assert raw[:8] == b'\x93NUMPY\x01\x00'
    size = struct.unpack_from('<H', raw, 8)[0]
    header = ast.literal_eval(raw[10:10 + size].decode())
    assert header['descr'] == '<f4'
    return struct.unpack('<' + str((len(raw)-10-size)//4) + 'I', raw[10+size:])
def nan(bits):
    return bits & 0x7f800000 == 0x7f800000 and bits & 0x7fffff != 0
rng = random.Random(163)
with tempfile.TemporaryDirectory(prefix='camel-row-bits-') as folder:
    folder = Path(folder)
    env['TILES_TEST_OUT'] = str(folder)
    for width in [3,33,257,1024]:
        patterns = [[0], [0x80000000], [1,0x80000001], [0x71800000,0x3f800000,0xf1800000,0x3f800000],
                    [0x3f800001,0xbf800000,1], [0x7f800000,0xff800000,0], [0x7fc12345,0xffc54321,1],
                    [0x00800000,0x007fffff,0x80000001]]
        rows = [[row[i % len(row)] for i in range(width)] for row in patterns]
        rows += [[rng.getrandbits(32) & 0xff7fffff for _ in range(width)] for _ in range(24)]
        write(folder/'input.npy', [b for row in rows for b in row], (len(rows),width))
        for vm in ['std::nvm','std::fvm','std::jit']:
            result = subprocess.run([str(binary),str(Path(__file__).with_suffix('.cml')),vm],cwd=root,env=env,capture_output=True,text=True,timeout=60)
            if result.returncode: raise RuntimeError(result.stdout+result.stderr)
            plan = json.loads(result.stdout)
            assert plan['kernels'] == 1 and plan['stages'][0]['implementation'] == 'row_region', plan
            cpu,gpu=read(folder/'cpu.npy'),read(folder/'gpu.npy')
            assert len(cpu)==len(gpu)==len(rows)*width
            for i,(a,b) in enumerate(zip(cpu,gpu)):
                assert a==b or (nan(a) and nan(b)), (vm,width,i,hex(a),hex(b))
    print('row bits: signed zeros, subnormals, nonfinite values and wide exponent fallback passed')
