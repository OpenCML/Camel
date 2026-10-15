"""Run the native CUDA module-lifetime test built by npm run build."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
build = root / 'build/modules/tiles'
name = 'tiles-module-lease.exe' if os.name == 'nt' else 'tiles-module-lease'
candidates = [build / 'Release' / name, build / name]
binary = next((p for p in candidates if p.is_file()), candidates[0])
subprocess.run([str(binary)], check=True)
