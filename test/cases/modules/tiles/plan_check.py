"""Run the native plan verifier built by npm run build."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
build = root / 'build/modules/tiles'
name = 'tiles-plan-check.exe' if os.name == 'nt' else 'tiles-plan-check'
candidates = [build / 'Release' / name, build / name]
binary = next((p for p in candidates if p.is_file()), candidates[0])
env = os.environ.copy()
env['PATH'] = str(root / 'out/latest/bin') + os.pathsep + env.get('PATH', '')
subprocess.run([str(binary)], env=env, check=True)
