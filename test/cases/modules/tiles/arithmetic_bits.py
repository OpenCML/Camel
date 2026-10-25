"""CPU/GPU basic operations compare raw f32 bits, with arithmetic NaN payloads unspecified."""
import ast
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'test' / 'tools'))
from camel_arg import camel_from_argv  # noqa: E402

# `--camel <path>` names the install this case measures; without it the install is
# this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py. `ARGV` is this process's arguments without the `--camel` pair.
EXE, ARGV = camel_from_argv(ROOT)
# The install that binary lives in: the child is given it as CAMEL_HOME, so the compiler the case
# runs and the standard library it reads are the same install whichever one `--camel` named.
INSTALL = str(Path(EXE).resolve().parents[1])

def write(path, values):
    header = str(dict(descr='<f4', fortran_order=False, shape=(len(values),)))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    path.write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header)) + header.encode()
                     + struct.pack('<' + str(len(values)) + 'I', *values))

def read(path):
    data = path.read_bytes()
    assert data[:8] == b'\x93NUMPY\x01\x00'
    size = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + size].decode())
    assert header['descr'] == '<f4' and not header['fortran_order']
    return struct.unpack('<' + str((len(data) - 10 - size) // 4) + 'I', data[10 + size:])

def nan(bits):
    return bits & 0x7f800000 == 0x7f800000 and bits & 0x007fffff != 0

special = [0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
           0x3f800000, 0xbf800000, 0x3f800001, 0x3f7fffff,
           0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321]
pairs = [(a, b) for a in special for b in special]
rng = random.Random(241016)
pairs += [(rng.getrandbits(32), rng.getrandbits(32)) for _ in range(4096)]
with tempfile.TemporaryDirectory(prefix='camel-arithmetic-bits-') as folder:
    folder = Path(folder)
    write(folder / 'a.npy', [a for a, _ in pairs])
    write(folder / 'b.npy', [b for _, b in pairs])
    env = dict(os.environ, CAMEL_HOME=INSTALL, TILES_TEST_OUT=str(folder))
    for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
        result = subprocess.run([EXE, str(Path(__file__).with_suffix('.cml')), vm],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
        for op, name in enumerate(['add', 'subtract', 'multiply', 'divide', 'sqrt', 'uncontracted']):
            cpu, gpu = read(folder / f'cpu{op}.npy'), read(folder / f'gpu{op}.npy')
            assert len(cpu) == len(gpu) == len(pairs)
            for i, (a, b) in enumerate(zip(cpu, gpu)):
                assert a == b or (nan(a) and nan(b)), (vm, name, i, tuple(hex(x) for x in pairs[i]), hex(a), hex(b))
print('Arithmetic bits: six operations, four schedulers, signed zeros and subnormals PASS')
