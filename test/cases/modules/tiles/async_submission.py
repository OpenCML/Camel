"""Run the native CUDA submission checks built by npm run build."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
name = 'tiles-async-check.exe' if os.name == 'nt' else 'tiles-async-check'
build = root / 'build/modules/tiles'
candidates = [build / 'Release' / name, build / name]
binary = next((path for path in candidates if path.is_file()), candidates[0])
env = os.environ.copy()
env['PATH'] = os.pathsep.join([str(root / 'out/latest/bin'), str(root / 'out/latest/libs'), env.get('PATH', '')])
if os.name != 'nt':
    env['LD_LIBRARY_PATH'] = str(root / 'out/latest/libs') + os.pathsep + env.get('LD_LIBRARY_PATH', '')
subprocess.run([str(binary)], env=env, check=True)
