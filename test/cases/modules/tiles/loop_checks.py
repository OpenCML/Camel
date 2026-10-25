"""Check CPU loop boundaries, fault propagation and local benchmark grants."""
import json
import os
from pathlib import Path
import subprocess
import sys

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
ENV = dict(os.environ, CAMEL_HOME=INSTALL,
           CAMEL_GC_VERIFY='both', CAMEL_GC_STRESS_SAFEPOINT='1', CAMEL_GC_STRESS_MODE='minor')

def run(case, passes, env=ENV):
    return subprocess.run([EXE, '--gc', 'generational', str(Path(__file__).with_name(case + '.cml')), *passes],
                          cwd=ROOT, env=env, capture_output=True, text=True, timeout=90)

for vm in ['std::nvm', 'std::fvm', 'std::jit', 'std::prl']:
    for hidden in [False, True]:
        env = dict(ENV)
        if hidden:
            env['CUDA_VISIBLE_DEVICES'] = '-1'
        for case, passes in [('loop_boundaries', ['tiles::compile', vm])]:
            result = run(case, passes, env)
            assert result.returncode == 0, (vm, hidden, case, result.stdout, result.stderr)
            assert result.stdout == 'cpu round 1\ncpu round 2\n504.0\n12296.0\n36.0\n', result.stdout
    for case, marker, diagnostic in [
        ('loop_fault', 'before fault once', 'division by zero at Tiles node'),
        ('loop_benchmark_fault', 'benchmark before fault once', 'division by zero at Tiles node')]:
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

result = run('loop_boundaries', ['tiles::compile', 'std::jgir'])
assert result.returncode == 0, result.stdout + result.stderr
graphs = {g['name']: g for g in json.loads(result.stdout)['graphs']}
for name in ['effect', 'crosslane', 'conditional']:
    nodes = graphs[name]['nodes']
    assert any(n['kind'] == 'LOOP' for n in nodes), (name, nodes)
    minimum = 1 if name == 'conditional' else 2
    assert sum(n.get('label') == 'tiles:run_preferred_resident' for n in nodes) >= minimum, (name, nodes)
print('LOOP CPU boundaries: neighboring GPU regions preserved PASS')
