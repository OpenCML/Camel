"""Run the native plan verifier built by npm run build."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / 'test' / 'tools'))

from native_check import environment, resolve  # noqa: E402

# The verifier is collected into the install's `bin/` by scripts/collect-out.js, from the list the
# build writes; this launcher asks the install rather than spelling a build directory. A launcher
# that spelled one (`build/lib/tiles/Release/...`) started failing the entry with "no such file"
# once the module moved to `src/stdlib/tiles/`, and that failure reads like a check refusing to hold
# while the check body had not started at all. See test/tools/native_check.py.
binary = resolve(root, 'tiles-plan-check.exe' if sys.platform == 'win32' else 'tiles-plan-check')
subprocess.run([str(binary)], env=environment(root), check=True)