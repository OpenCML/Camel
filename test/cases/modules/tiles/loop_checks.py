"""Check CPU loop boundaries, fault propagation and local benchmark grants."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get('CAMEL_EXE', str(ROOT / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')))
ENV = dict(os.environ, CAMEL_HOME=str(ROOT / 'out/latest'), CAMEL_GC='generational',
           CAMEL_GC_VERIFY='both', CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')

def run(case, passes, env=ENV):
    return subprocess.run([EXE, str(Path(__file__).with_name(case + '.cml')), *passes],
                          cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)

for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
    for hidden in [False, True]:
        env = dict(ENV)
        if hidden:
            env['CUDA_VISIBLE_DEVICES'] = '-1'
        for case, passes in [('loop_boundaries', ['std::tiles', vm]),
                             ('loop_decorator_boundaries', [vm])]:
            result = run(case, passes, env)
            assert result.returncode == 0, (vm, hidden, case, result.stdout, result.stderr)
            assert result.stdout == 'cpu round 1\ncpu round 2\n504.0\n12296.0\n36.0\n', result.stdout
    for case, marker, diagnostic in [
        ('loop_fault', 'before fault once', 'division by zero at Tiles node'),
        ('loop_benchmark_fault', 'benchmark before fault once', 'division by zero at Tiles node'),
        ('loop_empty_fault', 'zero lanes before fault once', 'RuntimeDiag::DivisionByZero')]:
        result = run(case, [vm])
        text = result.stdout + result.stderr
        assert result.returncode == 1 and diagnostic in text, (vm, case, text)
        assert text.count(marker) == 1 and '\nafter fault\n' not in text, text
    result = run('loop_benchmark', [vm])
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(result.stdout)
    for phase in ['correctness_before', 'correctness_after']:
        assert report[phase]['bitwise_elements'] == 2, report[phase]
        assert report[phase]['relaxed_elements'] == 1, report[phase]
        assert report[phase]['mismatches'] == 0, report[phase]
    print(f'{vm}: LOOP CPU boundaries, faults and benchmark permissions PASS')

result = run('loop_boundaries', ['std::tiles', 'std::jgir'])
assert result.returncode == 0, result.stdout + result.stderr
graphs = {g['name']: g for g in json.loads(result.stdout)['graphs']}
for name in ['effect', 'crosslane', 'conditional']:
    nodes = graphs[name]['nodes']
    assert any(n['kind'] == 'LOOP' for n in nodes), (name, nodes)
    minimum = 1 if name == 'conditional' else 2
    assert sum(n.get('label') == 'tiles:run_preferred_resident' for n in nodes) >= minimum, (name, nodes)
print('LOOP CPU boundaries: neighboring GPU regions preserved PASS')
