"""Core view maps preserve bits and handle noncanonical gradient reshapes.

Raw manual CLI was inspected before adding assertions:
  python test/cases/modules/tiles/core_views.py --prepare tmp/core-views-manual
  $env:CAMEL_HOME = "$PWD/out/latest"
  $env:TILES_TEST_OUT = "$PWD/tmp/core-views-manual"
  out/latest/bin/camel.exe test/cases/modules/tiles/core_views.cml std::macro std::nvm
"""
import argparse
import ast
import itertools
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

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

def prepare(folder):
    folder.mkdir(parents=True, exist_ok=True)
    bits = [0, 0x80000000, 1, 0x80000001, 0x7fc12345, 0x7f800000, 0xff800000, 0x3f800000,
            0x007fffff, 0x00800000, 0x7fc54321, 0x40400000] * 2
    header = str(dict(descr='<f4', fortran_order=False, shape=(2, 3, 4)))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    (folder / 'x.npy').write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header)) + header.encode()
                                   + struct.pack('<24I', *bits))
    return bits


def read(path):
    data = path.read_bytes()
    size = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + size].decode())
    assert header['descr'] == '<f4' and not header['fortran_order'], header
    payload = data[10 + size:]
    return tuple(header['shape']), struct.unpack('<' + str(len(payload) // 4) + 'I', payload)


def permuted(bits, shape, axes):
    """Independent row-major coordinate oracle, without backend/index-map helpers."""
    output_shape = tuple(shape[axis] for axis in axes)
    output = []
    for coordinate in itertools.product(*(range(extent) for extent in output_shape)):
        source = [0] * len(shape)
        for axis, value in zip(axes, coordinate):
            source[axis] = value
        linear = 0
        for value, extent in zip(source, shape):
            linear = linear * extent + value
        output.append(bits[linear])
    return output_shape, tuple(output)


def verify(text, folder, bits):
    report, source = text.split('EXPAND_SOURCE\n', 1)
    assert report.startswith('FIXED_INSPECT\n') and source.rstrip().endswith('VIEWS_COMPLETE'), text
    plan = json.loads(report.removeprefix('FIXED_INSPECT\n'))
    assert plan['kernels'] == 1, plan
    assert plan['graph_fact_instances'] == 1 and plan['specialized_instances'] == 0, plan
    expected = {
        'fixed': permuted(bits, (2, 3, 4), (2, 0, 1)),
        'config0': permuted(bits, (2, 3, 4), (2, 0, 1)),
        'config1': permuted(bits, (2, 3, 4), (1, 2, 0)),
        'chain': ((2, 2, 3), tuple(bits[b * 12 + c * 4 + 1 + 2 * a]
                                  for a in range(2) for b in range(2) for c in range(3))),
    }
    weights = (1.0, -2.0, 3.0, -4.0, 5.0, -6.0)
    weights_bits = struct.unpack('<6I', struct.pack('<6f', *weights))
    expanded = tuple(weights_bits[row * 3 + column]
                     for row in range(2) for repeat in range(4) for column in range(3))
    expected['expand'] = expected['negative'] = ((2, 4, 3), expanded)
    expected['keep'] = ((2, 4, 3), (0x3f800000,) * 24)
    expected['wide'] = ((2, 3, 4, 5), (0x3f800000,) * 120)
    # Only the empty view's shape/payload is an oracle here. No scalar empty
    # reduction result or its older no-fault behavior is asserted as semantics.
    expected['empty'] = ((0, 4, 2), ())
    for name, value in expected.items():
        if name.startswith('config'):
            suffix = name[-1]
            names = ['config_cpu' + suffix, 'config_gpu' + suffix]
        else:
            names = [name + '_cpu', name + '_gpu']
        for filename in names:
            actual = read(folder / (filename + '.npy'))
            assert actual == value, (filename, actual, value)
    assert read(folder / 'config_gpu2.npy') == expected['config0'], 'A/B/A context binding retained stale axes'


def run_matrix():
    with tempfile.TemporaryDirectory(prefix='camel-core-views-') as directory:
        folder = Path(directory)
        bits = prepare(folder)
        env = dict(os.environ, CAMEL_HOME=INSTALL, TILES_TEST_OUT=str(folder),
                   CAMEL_GC_VERIFY='both',
                   CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')
        for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
            result = subprocess.run([EXE, '--gc', 'generational',
                                     str(Path(__file__).with_suffix('.cml')), 'std::macro', vm],
                                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=120)
            try:
                assert result.returncode == 0, (vm, result.returncode, result.stdout, result.stderr)
                verify(result.stdout, folder, bits)
            except Exception:
                failure = ROOT / 'tmp' / ('core-views-failure-' + vm.replace('::', '-') + '-' + str(time.time_ns()))
                shutil.copytree(folder, failure)
                (failure / 'stdout.txt').write_text(result.stdout, encoding='utf-8')
                (failure / 'stderr.txt').write_text(result.stderr, encoding='utf-8')
                (failure / 'returncode.txt').write_text(str(result.returncode), encoding='utf-8')
                print('Core view failure fixtures retained:', failure, flush=True)
                raise
            print(f'{vm}: core views exact bits, core facts, gradient reshapes and fresh context PASS')

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', type=Path)
    args = parser.parse_args(ARGV)
    if args.prepare:
        prepare(args.prepare)
        print('Core view fixtures prepared:', args.prepare.resolve())
    else:
        run_matrix()
