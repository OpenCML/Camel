"""The hash proof of docs/design/gpu/plan.cn.md 4: two programs with the same nodes, graph
names and edges, a block whose region says `gpu` in one and `cpu` in the other, must hash
differently. `test::tags` prints the portable hash of every graph; this compares the two runs.

Run from a built tree (the plan runner does), with out/latest/bin/camel there.
"""

import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[4]
name = "camel.exe" if os.name == "nt" else "camel"
camel = root / "out" / "latest" / "bin" / name
env = os.environ.copy()
env["PATH"] = os.pathsep.join(
    [str(root / "out" / "latest" / "bin"), str(root / "out" / "latest" / "libs"),
     env.get("PATH", "")]
)
if os.name != "nt":
    env["LD_LIBRARY_PATH"] = os.pathsep.join(
        [str(root / "out" / "latest" / "libs"), env.get("LD_LIBRARY_PATH", "")]
    )


def hashes(case):
    run = subprocess.run(
        [str(camel), str(case), "test::tags"],
        cwd=root,
        env=env,
        capture_output=True,
        text=True,
    )
    if run.returncode not in (0, 5):  # 5: the tag has no reader in this pipeline (a warning)
        raise SystemExit(
            f"{case.name} failed ({run.returncode}):\n{run.stdout}\n{run.stderr}"
        )
    found = {}
    for line in run.stdout.splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[0] == "TAGS" and fields[1] == "hash":
            found[fields[2]] = fields[3]
    return found


gpu = hashes(root / "test/cases/modules/tiles/placement_hash_gpu.cml")
cpu = hashes(root / "test/cases/modules/tiles/placement_hash_cpu.cml")
assert "main" in gpu and "main" in cpu, f"no main hash: {gpu} {cpu}"
assert gpu["main"] != cpu["main"], (
    "the hashes are equal although only the block's placement differs: "
    f"{gpu['main']}"
)
# The other graphs are identical in both programs. `__root__` is skipped: the hash of a graph
# covers the graphs it calls, so the root differs only because `main` does.
assert "other" in gpu and "other" in cpu, f"no other hash: {gpu} {cpu}"
assert gpu["other"] == cpu["other"], "a graph without a block differs for another reason"
print(f"placement hash differs: gpu {gpu['main']} cpu {cpu['main']}")