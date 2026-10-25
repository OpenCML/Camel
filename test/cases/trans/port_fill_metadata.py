"""Verify port metadata and real node output types in all DOT exporters."""
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

# The install this case measures: `--camel <path>` names it (the author's ruling of 2026-10-25
# replaced the `CAMEL_EXE` environment switch with that argument); without it,
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py.
EXE, ARGV = camel_from_argv(ROOT)
env = dict(os.environ)
env.setdefault("CAMEL_HOME", str(Path(EXE).resolve().parents[1]))
for export in ("rgir", "gir", "argir", "agir"):
    result = subprocess.run([EXE, "--color", "never", str(Path(__file__).with_suffix(".cml")), "std::" + export], capture_output=True, text=True, env=env, cwd=ROOT)
    assert result.returncode == 0, result.stderr
    rows = [dict(re.findall(r'(\w+)="([^"]*)"', line)) for line in result.stdout.splitlines()]
    for name, kind, index in (("offset", "with", "0"), ("items", "normal", "0"), ("offset", "capture", "0"), ("count", "capture", "1"), ("pair", "capture", "0"), ("record", "capture", "1")):
        assert any(row.get("param_name") == name and row.get("param_kind") == kind and row.get("param_index") == index for row in rows), (export, name, kind)
    for value_type in ("int[]", "(int, int)", "{ value: int, size: int }", "() => int[]"):
        assert any(row.get("type") == "FILL" and row.get("value_type") == value_type for row in rows), (export, value_type)
    for kind, value_type in (("DATA", "int[]"), ("OPER", "bool"), ("CALL", "int[]"), ("FUNC", "int[]"), ("BRCH", "int"), ("JOIN", "int[]"), ("ACCS", "int")):
        assert any(row.get("type") == kind and row.get("value_type") == value_type for row in rows), (export, kind, value_type)
    assert not any(row.get("type") in ("ARGS", "EXIT") and row.get("value_type") for row in rows), "synthetic boundaries must not claim a node value type"
    loops = subprocess.run([EXE, str(ROOT / "test/cases/loops/captures.cml"), "std::" + export], capture_output=True, text=True, env=env, cwd=ROOT)
    assert loops.returncode == 0, loops.stderr
    loop_rows = [dict(re.findall(r'(\w+)="([^"]*)"', line)) for line in loops.stdout.splitlines()]
    assert {row["value_type"] for row in loop_rows if row.get("type") == "LOOP"} == {"int", "(int, int)"}, export
print("port/fill metadata: PASS")
