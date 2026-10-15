"""Lexical numeric grants control only the authorized CUDA contraction."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
for passes in [['std::nvm'], ['std::fvm'], ['std::jit'], ['std::inline', 'std::fvm'], ['std::opt', 'std::nvm']]:
    result = subprocess.run([str(binary), str(Path(__file__).with_suffix('.cml')), *passes], cwd=root, env=env, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    lines = result.stdout.splitlines()
    expected = [0.0, -6 * 2.0**-46, -6 * 2.0**-46, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]
    assert [float(x) for x in lines[:9]] == expected, (passes, lines[:9])
    plans = [json.loads(x) for x in lines[9:13]]
    assert plans[0]['numeric_permissions'] == [0, 0, 0, 2, 2], plans
    assert plans[1]['numeric_permissions'] == [0, 0, 0, 0, 0], plans
    assert plans[2]['numeric_permissions'] == [0, 0, 0, 0, 2], plans
    assert plans[3]['numeric_permissions'] == [0, 0, 0, 2, 0], plans
    assert all(p['kernels'] == 1 for p in plans), plans
    for plan in plans:
        assert {c['identity'] for c in plan['operator_contracts']} == {'tensor:multiply@1.0', 'tensor:add@1.0'}, plan
        assert all(c['declared'] and c['numeric'] == 'correctly_rounded' for c in plan['operator_contracts']), plan
    assert '\n'.join(lines[13:]).count('=fmaf(') == 1
    print(' '.join(passes) + ': numeric permission boundaries, broadcast FMA and strict reuse passed')
