"""Check the optimization namespace and the inline shortcut against the real CLI."""
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]
EXE = ROOT / "out/latest/bin" / ("camel.exe" if os.name == "nt" else "camel")
CASE = ROOT / "test/cases/opt/inline/consecutive_sync_calls.cml"


def run(*passes):
    return subprocess.run(
        [str(EXE), str(CASE), *passes], cwd=ROOT, capture_output=True, text=True
    )


for suffix in ("", "::small", "::arm", "::hybrid"):
    canonical = run("std::opt::inline" + suffix, "std::gir")
    shortcut = run("std::inline" + suffix, "std::gir")
    assert canonical.returncode == shortcut.returncode == 0, canonical.stderr + shortcut.stderr
    assert canonical.stdout == shortcut.stdout, f"inline alias differs: {suffix}"

for stage in ("devirtualize", "specialize", "fold", "cse", "dce", "order"):
    result = run("std::opt::" + stage, "std::nvm")
    assert result.returncode == 0, result.stderr
    assert result.stdout.split() == ["1", "11", "203", "4"], result.stdout

# `std::opt::ctrl` was renamed to `std::opt::order` (docs/design/effects/edges.cn.md 5.4): the old
# name is not an alias, so a pipeline that still writes it is refused rather than silently reduced.
for retired in ("std::devirtualize", "std::specialize", "std::opt::simplify", "std::opt::ctrl"):
    result = run(retired, "std::gir")
    assert result.returncode != 0, f"retired entry still registered: {retired}"

print("Optimization namespace and inline aliases: PASS")
