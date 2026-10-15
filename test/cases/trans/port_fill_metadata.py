"""Verify port names and assembled types in all DOT exporters."""
import os
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXE = os.environ.get("CAMEL_EXE", str(ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")))
env = dict(os.environ)
env.setdefault("CAMEL_HOME", str(ROOT / "out/latest"))
for export in ("rgir", "gir", "argir", "agir"):
    result = subprocess.run([EXE, "--color", "never", str(Path(__file__).with_suffix(".cml")), "std::" + export], capture_output=True, text=True, env=env, cwd=ROOT)
    assert result.returncode == 0, result.stderr
    rows = [dict(re.findall(r'(\w+)="([^"]*)"', line)) for line in result.stdout.splitlines()]
    for name, kind, index in (("offset", "with", "0"), ("items", "normal", "0"), ("offset", "capture", "0"), ("count", "capture", "1"), ("pair", "capture", "0"), ("record", "capture", "1")):
        assert any(row.get("param_name") == name and row.get("param_kind") == kind and row.get("param_index") == index for row in rows), (export, name, kind)
    for value_type in ("int[]", "(int, int)", "{ value: int, size: int }", "() => int[]"):
        assert any(row.get("type") == "FILL" and row.get("value_type") == value_type for row in rows), (export, value_type)
print("port/fill metadata: PASS")
