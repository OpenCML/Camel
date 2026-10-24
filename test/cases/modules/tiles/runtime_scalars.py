"""Verify changing host scalar inputs reuses code while preserving values."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
binary = root / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')
env = os.environ.copy()
env['CAMEL_HOME'] = str(root / 'out/latest')
for vm in ['std::nvm', 'std::fvm', 'std::jit']:
    result = subprocess.run([str(binary), str(Path(__file__).with_suffix('.cml')), vm],
                            cwd=root, env=env, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise RuntimeError(result.stdout + result.stderr)
    lines = result.stdout.splitlines()
    assert len(lines) == 12, lines
    assert lines[:3] == ['true'] * 3, 'scalar arguments or captures changed CUDA source'
    assert lines[3:9] == ['0.0'] * 6, 'scalar input value or precision lost'
    plan = json.loads(lines[9])
    # `scale`'s scalar takes the element type beside it (design principles 7.3), so the kernel is
    # the multiply alone: an f32 scalar is an operand of it, not a value a conversion node turns
    # into one, which is why the stage has one member and not two.
    assert plan['kernels'] == 1 and len(plan['stages'][0]['members']) == 1, plan
    reports = [json.loads(line) for line in lines[10:]]
    assert reports[1]['optimized']['compile_cache_hit'], 'changed scalar values missed code cache'
    for report in reports:
        assert report['correctness_before']['mismatches'] == 0, report
        assert report['correctness_after']['mismatches'] == 0, report
    print(f'{vm}: runtime scalar values, captured inputs and code-cache reuse passed')
