"""Run the native CUDA module-lifetime test built by npm run build."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
# The module's build directory follows the module: `build/modules/tiles` while it lives in
# `modules/tiles`, `build/lib/tiles` now that it lives in `lib/tiles`. Either answers, so this case
# runs against both trees.
build = next((p for p in (root / 'build/lib/tiles', root / 'build/modules/tiles') if p.is_dir()), root / 'build/lib/tiles')
name = 'tiles-module-lease.exe' if os.name == 'nt' else 'tiles-module-lease'
candidates = [build / 'Release' / name, build / name]
binary = next((p for p in candidates if p.is_file()), candidates[0])
subprocess.run([str(binary)], check=True)
