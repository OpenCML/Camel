"""Run the native CUDA submission checks built by npm run build."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
name = 'tiles-async-check.exe' if os.name == 'nt' else 'tiles-async-check'
# The module's build directory follows the module: `build/modules/tiles` while it lives in
# `modules/tiles`, `build/lib/tiles` now that it lives in `lib/tiles`. Either answers, so this case
# runs against both trees.
build = next((p for p in (root / 'build/lib/tiles', root / 'build/modules/tiles') if p.is_dir()), root / 'build/lib/tiles')
candidates = [build / 'Release' / name, build / name]
binary = next((path for path in candidates if path.is_file()), candidates[0])
env = os.environ.copy()
env['PATH'] = os.pathsep.join([str(root / 'out/latest/bin'), str(root / 'out/latest/libs'), env.get('PATH', '')])
if os.name != 'nt':
    env['LD_LIBRARY_PATH'] = str(root / 'out/latest/libs') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
subprocess.run([str(binary)], env=env, check=True)
