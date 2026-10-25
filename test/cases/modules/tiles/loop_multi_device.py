"""Multi-state LOOP fixtures; --prepare DIR enables a raw manual Camel run first.

Manual verification after the standard build (PowerShell, repository root):
  python test/cases/modules/tiles/loop_multi_device.py --prepare out/loop-multi-manual
  $env:CAMEL_HOME = "$PWD/out/latest"
  $env:TILES_TEST_OUT = "$PWD/out/loop-multi-manual"
  out/latest/bin/camel.exe test/cases/modules/tiles/loop_multi_device.cml std::nvm
  out/latest/bin/camel.exe test/cases/modules/tiles/loop_multi_device.cml tiles::compile std::nvm
  out/latest/bin/camel.exe test/cases/modules/tiles/loop_multi_device.cml tiles::compile std::jgir
"""
import argparse
import ast
import json
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

# `--camel <path>` names the install this case measures: the author's ruling of 2026-10-25 replaced
# the `CAMEL_EXE` environment switch with that argument. Without it the install is
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
    if header['descr'] != '<f4':
        raise ValueError('expected f32 fixture')
    return struct.unpack('<' + str((len(data) - 10 - size) // 4) + 'I', data[10 + size:])


def prepare(folder):
    folder.mkdir(parents=True, exist_ok=True)
    rng = random.Random(24101817)
    a = [0, 0x80000000, 1, 0x80000001, 0x007fffff, 0x00800000,
         0x4b800000, 0x3f800001, 0x7f800000, 0xff800000, 0x7fc12345]
    b = [0x3f800000] * len(a)
    a += [rng.getrandbits(32) for _ in range(256)]
    b += [rng.getrandbits(32) for _ in range(256)]
    c = [0x3e800000] * len(a)
    for name, bits in [('a', a), ('b', b), ('c', c)]:
        write(folder / (name + '.npy'), bits)
    return a, b


def nan(bits):
    return bits & 0x7f800000 == 0x7f800000 and bits & 0x007fffff != 0


def verify_device(text, folder, a, b):
    # These exact values were first observed through the raw manual CLI.
    expected = ['(7.0, 2.0) (7.0, 2.0)', '(5.0, 8.0) (5.0, 8.0)',
                '(11.0, 2.0, 7.0) (11.0, 2.0, 7.0)']
    assert text.split('SCALAR_INSPECT\n')[0].splitlines() == ['VALUES', *expected], text
    markers = ['SCALAR_INSPECT', 'PAIR_INSPECT', 'FIRST_INSPECT', 'ORDER_INSPECT',
               'PAIR_SOURCE', 'PERMISSIONS_SOURCE', 'PERMISSIONS_BENCHMARK',
               'PROJECTION_BENCHMARK', 'ONE_BENCHMARK', 'TWO_BENCHMARK', 'EMPTY_BENCHMARK']
    sections = {}
    for i, marker in enumerate(markers):
        value = text.split(marker + '\n', 1)[1]
        sections[marker] = value.split('\n' + markers[i + 1] + '\n', 1)[0] if i + 1 < len(markers) else value
    plans = [json.loads(sections[name]) for name in markers[:4]]
    for plan in plans:
        assert plan['kernels'] == 1 and len(plan['stages']) == 1, plan
        produced = plan['stages'][0]['outputs']
        assert len(produced) == 2 and len(set(produced)) == 2, plan
    assert len(plans[2]['outputs']) == 1, plans[2]
    assert plans[3]['outputs'] == [plans[3]['stages'][0]['outputs'][1],
                                   plans[3]['stages'][0]['outputs'][0],
                                   plans[3]['stages'][0]['outputs'][1]], plans[3]
    source = sections['PAIR_SOURCE']
    assert len(re.findall(r'for\([^\n]*k_iteration=', source)) == 1, source
    assert source.count('__fadd_rn(') == 1, source
    # Every next-state binding must precede every current-state assignment (swap safety).
    assert source.index('k_next1=') < source.index('k_loop_input0=k_next0;'), source
    permissions = sections['PERMISSIONS_SOURCE']
    assert '__fmul_rn(' in permissions and '__fadd_rn(' in permissions, permissions
    pairs = [('cpu0', 'gpu0'), ('cpu1', 'gpu1'), ('cpu0', 'first'), ('cpu1', 'second'),
             ('cpu0', 'resident0'), ('cpu1', 'resident1'), ('cpu1', 'order0'),
             ('cpu0', 'order1'), ('cpu1', 'order2')]
    pairs += [(f'capcpu{i}', f'capgpu{i}') for i in range(4)]
    pairs += [('capcpu0', 'capgpu4'), ('capcpu1', 'capgpu5')]
    for cpu_name, gpu_name in pairs:
        cpu, gpu = read(folder / (cpu_name + '.npy')), read(folder / (gpu_name + '.npy'))
        assert len(cpu) == len(gpu) == len(a), (cpu_name, gpu_name)
        for i, (x, y) in enumerate(zip(cpu, gpu)):
            assert x == y or (nan(x) and nan(y)), (cpu_name, gpu_name, i, hex(x), hex(y))
    for name, bits in [('swap0', b), ('swap1', a), ('empty0', a), ('empty1', b)]:
        assert read(folder / (name + '.npy')) == tuple(bits), name + ' must preserve exact initial bits'
    assert read(folder / 'rounded.npy')[6] == 0x4b800000, 'each round must round f32'
    counts = [('PERMISSIONS_BENCHMARK', len(a), len(a)), ('PROJECTION_BENCHMARK', len(a), 0),
              ('ONE_BENCHMARK', len(a), 0), ('TWO_BENCHMARK', 0, len(a)),
              ('EMPTY_BENCHMARK', len(a), 0)]
    for marker, bitwise, relaxed in counts:
        report = json.loads(sections[marker])
        for phase in ['correctness_before', 'correctness_after']:
            assert report[phase]['bitwise_elements'] == bitwise, (marker, report[phase])
            assert report[phase]['relaxed_elements'] == relaxed, (marker, report[phase])
            assert report[phase]['mismatches'] == 0, (marker, report[phase])


def resident_matrix():
    with tempfile.TemporaryDirectory(prefix='camel-loop-multi-') as folder:
        folder = Path(folder)
        a, b = prepare(folder)
        env = dict(os.environ, CAMEL_HOME=INSTALL, TILES_TEST_OUT=str(folder),
                   CAMEL_GC_VERIFY='both',
                   CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')
        for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
            result = subprocess.run([EXE, '--gc', 'generational',
                                     str(Path(__file__).with_suffix('.cml')), vm],
                                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            assert result.returncode == 0, (vm, result.stdout, result.stderr)
            verify_device(result.stdout, folder, a, b)
            print(f'{vm}: multi-state LOOP bits, projections, captures, residency, grants and one launch PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', type=Path)
    args = parser.parse_args(ARGV)
    if args.prepare:
        prepare(args.prepare)
        print('Multi-state LOOP manual fixtures prepared:', args.prepare.resolve())
    else:
        resident_matrix()
