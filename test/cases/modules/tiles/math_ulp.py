"""Check GPU transcendental results against high-precision mathematical references."""
import ast
from decimal import Decimal, localcontext
import json
import math
import os
from pathlib import Path
import random
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get('CAMEL_EXE', str(ROOT / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')))


def write(path, bits):
    header = str(dict(descr='<f4', fortran_order=False, shape=(len(bits),)))
    header += ' ' * ((-(10 + len(header) + 1)) % 64) + '\n'
    path.write_bytes(b'\x93NUMPY\x01\x00' + struct.pack('<H', len(header)) + header.encode()
                     + struct.pack('<' + str(len(bits)) + 'I', *bits))


def read(path):
    data = path.read_bytes()
    assert data[:8] == b'\x93NUMPY\x01\x00'
    size = struct.unpack_from('<H', data, 8)[0]
    header = ast.literal_eval(data[10:10 + size].decode())
    assert header['descr'] == '<f4' and not header['fortran_order']
    result = struct.unpack('<' + str((len(data) - 10 - size) // 4) + 'I', data[10 + size:])
    assert header['shape'] == (len(result),)
    return result


def value(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def bits(value):
    return struct.unpack('<I', struct.pack('<f', value))[0]


def reference(name, x):
    if math.isnan(x) or (name == 'log' and x < 0):
        return 'nan'
    if name == 'log' and x == 0:
        return 0xff800000
    if math.isinf(x):
        return 0 if x < 0 else 0x7f800000
    if name == 'exp' and x == 0:
        return 0x3f800000
    if name == 'log' and x == 1:
        return 0
    exact = Decimal.from_float(x)
    return exact.exp() if name == 'exp' else exact.ln()


def check(name, inputs, outputs, bound):
    assert len(inputs) == len(outputs)
    worst = Decimal(0)
    for source, result in zip(inputs, outputs):
        expected = reference(name, value(source))
        if expected == 'nan':
            assert math.isnan(value(result)), (name, hex(source), hex(result))
        elif isinstance(expected, int):
            assert result == expected, (name, hex(source), hex(result), hex(expected))
        else:
            actual = value(result)
            assert math.isfinite(actual), (name, hex(source), hex(result))
            magnitude = abs(expected)
            exponent = math.frexp(float(magnitude))[1] - 1
            # Correct the floating estimate at exact binade boundaries.
            while magnitude < Decimal(2) ** exponent:
                exponent -= 1
            while magnitude >= Decimal(2) ** (exponent + 1):
                exponent += 1
            ulp = Decimal(2) ** max(-149, exponent - 23)
            error = abs(Decimal.from_float(actual) - expected) / ulp
            assert error <= bound, (name, hex(source), hex(result), str(error), str(bound))
            worst = max(worst, error)
    return float(worst)


rng = random.Random(241017)
special = [0, 0x80000000, 1, 0x007fffff, 0x00800000, 0x3f800000,
           0x3f7fffff, 0x3f800001, 0x7f800000, 0xff800000, 0x7fc12345, 0xbf800000]
inputs = {
    'exp': special + [bits(x) for x in [-110, -104, -103, -90, -88, -87, 80, 88]]
           + [bits(rng.uniform(-110, 88)) for _ in range(1024)],
    'log': special + [rng.randrange(1, 0x7f800000) for _ in range(1024)],
}
with localcontext() as context, tempfile.TemporaryDirectory(prefix='camel-math-ulp-') as folder:
    context.prec = 100
    folder = Path(folder)
    for name, data in inputs.items():
        write(folder / f'{name}.npy', data)
    env = dict(os.environ, CAMEL_HOME=str(ROOT / 'out/latest'), TILES_TEST_OUT=str(folder))
    for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
        run = subprocess.run([EXE, str(Path(__file__).with_suffix('.cml')), vm],
                             cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, run.stdout + run.stderr
        records = [json.loads(line) for line in run.stdout.splitlines()]
        assert len(records) == 2
        for name, record in zip(['exp', 'log'], records):
            contract, = record['operator_contracts']
            assert contract['identity'].startswith(f'tensor:{name}@')
            assert contract['declared'] and contract['numeric'] == 'bounded'
            bound = Decimal(str(contract['ulp_bound']))
            assert bound.is_finite() and bound > 0
            worst = check(name, inputs[name], read(folder / f'{name}_result.npy'), bound)
            print(f'{vm} {name}: {len(inputs[name])} inputs, worst {worst:.6f} ulp, declared {bound}')
print('Transcendental declared ULP bounds: four schedulers PASS')
