"""Static LOOP executes on CUDA with sequential IEEE state and fresh captures."""
import ast
import os
from pathlib import Path
import random
import re
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'test' / 'tools'))
from camel_arg import camel_from_argv  # noqa: E402

# `--camel <path>` names the install this case measures; without it the install is
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
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
    size = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + size].decode())
    assert header['descr'] == '<f4'
    return struct.unpack('<' + str((len(data) - 10 - size) // 4) + 'I', data[10 + size:])

def nan(bits):
    return bits & 0x7f800000 == 0x7f800000 and bits & 0x007fffff != 0

rng = random.Random(241018)
a = [0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
     0x4b800000, 0x3f800001, 0x7f800000, 0xff800000, 0x7fc12345]
b = [0x3f800000] * len(a)
a += [rng.getrandbits(32) for _ in range(256)]
b += [rng.getrandbits(32) for _ in range(256)]
with tempfile.TemporaryDirectory(prefix='camel-loop-device-') as folder:
    folder = Path(folder)
    write(folder / 'a.npy', a)
    write(folder / 'b.npy', b)
    env = dict(os.environ, CAMEL_HOME=INSTALL, TILES_TEST_OUT=str(folder),
               CAMEL_GC_VERIFY='both',
               CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')
    for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
        result = subprocess.run([EXE, '--gc', 'generational',
                                 str(Path(__file__).with_suffix('.cml')), vm],
                                cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
        assert result.returncode == 0, (vm, result.stdout, result.stderr)
        values, source = result.stdout.split('LOOP_SOURCE\n', 1)
        for line in values.splitlines():
            cpu, gpu = line.split()
            assert cpu == gpu, (vm, line)
        assert len(re.findall(r'for\([^\n]*k_iteration=', source)) == 1, source
        assert source.count('__fmul_rn(') == source.count('__fadd_rn(') == 1, source
        for cpu_id, gpu_id in [(0, 0), (1, 1), (0, 2), (3, 3)]:
            cpu, gpu = read(folder / f'cpu{cpu_id}.npy'), read(folder / f'gpu{gpu_id}.npy')
            assert len(cpu) == len(gpu) == len(a)
            for i, (x, y) in enumerate(zip(cpu, gpu)):
                assert x == y or (nan(x) and nan(y)), (vm, cpu_id, i, hex(x), hex(y))
        assert read(folder / 'gpu3.npy') == tuple(a), 'empty loop changes initial bits'
        assert read(folder / 'gpu4.npy')[6] == 0x4b800000, 'each round must round f32'
        print(f'{vm}: LOOP f64, f32 bits, captures, empty domain and device source PASS')
