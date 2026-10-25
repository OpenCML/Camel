"""Check public synchronous completion after raw CLI acceptance.

  python test/cases/modules/tiles/async_execution.py --prepare tmp/async-execution-manual
  $env:TILES_TEST_OUT = "$PWD/tmp/async-execution-manual"
  out/latest/bin/camel.exe test/cases/modules/tiles/async_execution.cml std::macro std::nvm
"""
import argparse
import ast
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

# `--camel <path>` names the install this case measures: the author's ruling of 2026-10-25 replaced
# the `CAMEL_EXE` environment switch with that argument. Without it the install is
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py. `ARGV` is this process's arguments without the `--camel` pair.
EXE, ARGV = camel_from_argv(ROOT)
# The install that binary lives in: the child is given it as CAMEL_HOME, so the compiler the case
# runs and the standard library it reads are the same install whichever one `--camel` named.
INSTALL = str(Path(EXE).resolve().parents[1])


def prepare(folder):
    folder.mkdir(parents=True, exist_ok=True)
    # Finite dyadics keep the independent four-step arithmetic oracle exact.
    values = [0.0, -0.0, 0.5, -0.5, 1.0, -1.0, 3.0, -7.0] * 3
    header = str(dict(descr='<f4', fortran_order=False, shape=(2, 3, 4)))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    (folder / 'x.npy').write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header))
                                   + header.encode() + struct.pack('<24f', *values))
    return values


def read(path):
    data = path.read_bytes()
    assert data[:8] == b'\x93NUMPY\x01\x00', path
    length = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + length].decode())
    assert header['descr'] == '<f4' and not header['fortran_order'], header
    return tuple(header['shape']), data[10 + length:]


def verify(folder, values):
    original = struct.pack('<24f', *values)
    first = struct.pack('<24f', *(2 * x + 1 for x in values))
    chain = struct.pack('<24f', *(16 * x + 15 for x in values))
    expected = {'original': original, 'identity': original, 'pair1': original,
                'first': first, 'pair0': first, 'pair2': first, 'chain': chain}
    for name, payload in expected.items():
        assert read(folder / (name + '.npy')) == ((2, 3, 4), payload), name
    assert read(folder / 'empty.npy') == ((0, 3), b'')


def run_matrix():
    with tempfile.TemporaryDirectory(prefix='camel-async-public-') as temporary:
        base = Path(temporary)
        env = os.environ.copy()
        env.update(CAMEL_HOME=INSTALL,
                   CAMEL_GC_VERIFY='both',
                   CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')
        for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
            folder = base / vm.replace('::', '-')
            values = prepare(folder)
            env['TILES_TEST_OUT'] = str(folder)
            result = subprocess.run([EXE, '--gc', 'generational',
                                     str(Path(__file__).with_suffix('.cml')), 'std::macro', vm],
                                    cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)
            fault = subprocess.run([EXE, '--gc', 'generational',
                                    str(Path(__file__).with_name('async_execution_fault.cml')), 'std::macro', vm],
                                   cwd=ROOT, env=env, capture_output=True, text=True, timeout=30)
            try:
                assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
                assert result.stdout.strip() == 'ASYNC_PUBLIC_COMPLETE', result.stdout
                verify(folder, values)
                diagnostic = fault.stdout + fault.stderr
                assert fault.returncode == 1, (fault.returncode, diagnostic)
                assert 'division by zero at Tiles node' in diagnostic, diagnostic
                assert 'UNREACHABLE_AFTER_DEVICE_FAULT' not in diagnostic, diagnostic
            except Exception:
                retained = ROOT / 'tmp' / ('async-public-failure-' + vm.replace('::', '-') + '-' + str(time.time_ns()))
                shutil.copytree(folder, retained)
                for name, process in [('public', result), ('fault', fault)]:
                    (retained / (name + '-stdout.txt')).write_text(process.stdout, encoding='utf-8')
                    (retained / (name + '-stderr.txt')).write_text(process.stderr, encoding='utf-8')
                    (retained / (name + '-returncode.txt')).write_text(str(process.returncode), encoding='utf-8')
                print('Async public failure retained:', retained, flush=True)
                raise
            print(f'{vm}: async public exact bits, completed results and eager faults PASS')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare', type=Path)
    args = parser.parse_args(ARGV)
    if args.prepare:
        prepare(args.prepare)
        print('Async execution fixtures prepared:', args.prepare.resolve())
    else:
        run_matrix()
