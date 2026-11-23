"""Run the native CUDA submission checks built by npm run build."""
import subprocess
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(root / 'test' / 'tools'))

from native_check import environment, resolve  # noqa: E402

# The check is collected into the install's `bin/` by scripts/collect-out.js, from the list the build
# writes; this launcher asks the install rather than spelling a build directory it would have to keep
# in step with the layout. See test/tools/native_check.py.
binary = resolve(root, 'tiles-async-check.exe' if sys.platform == 'win32' else 'tiles-async-check')
subprocess.run([str(binary)], env=environment(root), check=True)