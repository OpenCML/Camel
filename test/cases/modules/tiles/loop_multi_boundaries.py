"""Mixed state domains and cross-lane bodies stay on CPU between GPU regions.

The raw tiles::compile/std::nvm and tiles::compile/std::jgir commands were inspected before
these assertions were added. This driver also verifies decorator rewriting at runtime.
"""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get('CAMEL_EXE', str(ROOT / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')))
ENV = dict(os.environ, CAMEL_HOME=str(ROOT / 'out/latest'), CAMEL_GC='generational',
           CAMEL_GC_VERIFY='both', CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')


def run(passes, env=ENV):
    return subprocess.run([EXE, str(Path(__file__).with_suffix('.cml')), *passes],
                          cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)


for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
    for hidden in [False, True]:
        env = dict(ENV)
        if hidden:
            env['CUDA_VISIBLE_DEVICES'] = '-1'
        for passes in [[vm], ['tiles::compile', vm]]:
            result = run(passes, env)
            assert result.returncode == 0, (passes, hidden, result.stdout, result.stderr)
            lines = result.stdout.splitlines()
            assert lines[:5] == ['18.0 30.0 18.0 30.0', '83.0 83.0', '2504.0 2504.0',
                                 '18.0 18.0', '116.0 116.0'], (passes, hidden, lines)
            assert len(lines) == 7, lines
            for line in lines[5:]:
                graph = json.loads(line)
                if vm == 'std::nvm' or passes[0] == 'tiles::compile':
                    assert graph['regions'] == [], graph
                    assert sum(n['reason'] == 'context_arguments' for n in graph['excluded']) == 1, graph
                else:
                    # Lowered backends give graph-inspecting operators the original
                    # structured graph. The decorator's private runtime copy is separate.
                    assert len(graph['regions']) == 1, graph
                    assert len(graph['regions'][0]['outputs']) == 1, graph
    print(f'{vm}: multi-state LOOP pass/decorator, CPU boundaries and hidden CUDA PASS')

result = run(['tiles::compile', 'std::jgir'])
assert result.returncode == 0, result.stdout + result.stderr
graphs = {g['name']: g for g in json.loads(result.stdout)['graphs']}
for name in ['mixed', 'crosslane', 'differing']:
    nodes = graphs[name]['nodes']
    assert any(n['kind'] == 'LOOP' for n in nodes), (name, nodes)
    assert sum(n.get('label') == 'tiles:run_preferred_resident' for n in nodes) >= 2, (name, nodes)
for name, downloads in [('eligible', 2), ('decorated', 2), ('selected', 1), ('decorated_selected', 1)]:
    nodes = graphs[name]['nodes']
    assert not any(n['kind'] == 'LOOP' for n in nodes), (name, nodes)
    assert sum(n.get('label') == 'tiles:run_preferred_resident' for n in nodes) == 1, (name, nodes)
    assert sum(n.get('label') == 'tiles:download' for n in nodes) == downloads, (name, nodes)
print('Multi-state LOOP graph: one region, selected downloads and CPU neighbor regions PASS')
