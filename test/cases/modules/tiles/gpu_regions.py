"""The decorator outlines static regions once and preserves CPU effects and captures."""
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[4]
EXE = os.environ.get('CAMEL_EXE', str(ROOT / 'out/latest/bin' / ('camel.exe' if os.name == 'nt' else 'camel')))
for hidden in [False, True]:
    env = dict(os.environ, CAMEL_HOME=str(ROOT / 'out/latest'), CAMEL_GC_STRESS='1')
    if hidden:
        env['CUDA_VISIBLE_DEVICES'] = '-1'
    for passes in [['std::nvm'], ['std::fvm'], ['std::jit'], ['std::prl'], ['std::macro', 'std::nvm']]:
        run = subprocess.run([EXE, str(Path(__file__).with_suffix('.cml')), *passes],
                             cwd=ROOT, env=env, capture_output=True, text=True, timeout=60)
        assert run.returncode == 0, (hidden, passes, run.stdout, run.stderr)
        lines = run.stdout.splitlines()
        assert len(lines) == 4 and lines[:2] == ['cpu detour once', '56.0'] and lines[3] == '12.0', lines
        graph = json.loads(lines[2])
        assert graph['regions'] == [], graph
        # Each outlined call has a context argument holding its extracted graph.
        # The old whole-function wrapper has one such call, rather than two regions.
        assert sum(n['reason'] == 'context_arguments' for n in graph['excluded']) == 2, graph
print('GPU decorator regions: mixed execution, captures, macro and CPU fallback PASS')
