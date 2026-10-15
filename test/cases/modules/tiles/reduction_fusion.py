"""Check ordered fused reduction mappings and exact result bytes on each host VM."""
import os
from pathlib import Path
import json
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
with tempfile.TemporaryDirectory(prefix='camel-reduction-fusion-') as folder:
    env['TILES_TEST_OUT'] = folder
    for vm in ['std::nvm', 'std::fvm', 'std::jit']:
        result = subprocess.run([str(binary), str(Path(__file__).with_suffix('.cml')), vm],
                                env=env, cwd=root, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        plans = [json.loads(line) for line in result.stdout.splitlines()]
        assert len(plans) == 6, plans
        for i, plan in enumerate(plans):
            assert plan['kernels'] == (2 if i == 4 else 1), plan
            assert all(stage['implementation'] == 'ordered_reduce' for stage in plan['stages']), plan
            assert plan['allocated_bytes'] < plan['logical_bytes'], plan
        for name in ['rows', 'columns', 'middle', 'ordered', 'empty', 'no_rows',
                     'epilogue', 'two_reductions', 'rounding']:
            cpu = (Path(folder) / f'{name}_cpu.npy').read_bytes()
            gpu = (Path(folder) / f'{name}_gpu.npy').read_bytes()
            assert cpu == gpu, (vm, name)
        ordered = (Path(folder) / 'ordered_gpu.npy').read_bytes()
        offset = 10 + struct.unpack_from('<H', ordered, 8)[0]
        assert struct.unpack('<f', ordered[offset:]) == (0.5,), 'lost ordered f64 accumulator'
        rounded = (Path(folder) / 'rounding_gpu.npy').read_bytes()
        offset = 10 + struct.unpack_from('<H', rounded, 8)[0]
        assert struct.unpack('<I', rounded[offset:]) == (0,), 'lost reduction f32 rounding before epilogue'
        print(f'{vm}: fused reduction bytes, mappings and empty domains passed')
