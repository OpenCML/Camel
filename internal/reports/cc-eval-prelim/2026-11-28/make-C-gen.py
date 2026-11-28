"""Generate n progen programs into the CC-eval preliminary report and write the plan that runs them.

Instrument for the stage-fault reading (tab:stage-faults), first half: a program's output under each
pipeline prefix must equal the reference that model.py computes, which is the archive's e9 step
without its container. Writes, beside this script:
  gen/prog_<seed>.cml      the program
  gen/index.tsv            seed, source path, reference lines (JSON list), attempts
  C-gen-prefix.plan.toml   three cells per program: no pass, opt::fold, opt::cse
"""
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
TREE = HERE.parents[3]
sys.path.insert(0, str(TREE / "test" / "tools" / "progen"))

from generate import DEFAULT_FEATURES, generate  # noqa: E402
from model import emit  # noqa: E402

N = int(sys.argv[1]) if len(sys.argv) > 1 else 100
GEN = HERE / "gen"
GEN.mkdir(exist_ok=True)

index = []
for seed in range(1, N + 1):
    prog, expected, attempts = generate(seed, DEFAULT_FEATURES, size=1)
    src = emit(prog)
    path = GEN / f"prog_{seed:03d}.cml"
    path.write_text(src, encoding="utf-8")
    index.append(dict(seed=seed, path=f"gen/prog_{seed:03d}.cml", expected=expected, attempts=attempts))

(HERE / "gen" / "index.json").write_text(json.dumps(index, indent=1), encoding="utf-8")

lines = []
lines.append("# Instrument, not a test: generated programs and their reference outputs, read once into")
lines.append("# the stage-fault reading beside it. It belongs to no layer.")
lines.append('id = "cc-eval-prelim.C-gen-prefix"')
lines.append("order = 996")
lines.append('tier = "fast"')
lines.append('tags = ["probe"]')
lines.append('desc = "A generated program output under each pipeline prefix, against the reference output the model computes: the archive differential test, first half, on this tip."')
lines.append("")
lines.append("[defaults]")
lines.append('kind = "behavior"')
lines.append("expect_exit = 0")
lines.append("timeout_ms = 20000")
lines.append('normalizers = ["strip_ansi", "normalize_newlines", "normalize_origins", "normalize_paths"]')


def esc(text):
    return (text.replace("\\", "\\\\").replace('"', '\\"')
                .replace("\r", "").replace("\n", "\\n").replace("\t", "\\t"))


for row in index:
    seed = row["seed"]
    ref = "".join(line + "\n" for line in row["expected"])
    for suffix, passes in (("p00_none", []), ("p01_fold", ["opt::fold"]), ("p02_cse", ["opt::cse"])):
        args = ["${case}"] + passes + ["std::nvm"]
        arglist = ", ".join(f'"{a}"' for a in args)
        lines.append("")
        lines.append("[[tests]]")
        lines.append(f'name = "C_gen.s{seed:03d}.{suffix}"')
        lines.append(f'case = "{row["path"]}"')
        lines.append(f"args = [{arglist}]")
        lines.append(f'expect_stdout_exact = "{esc(ref)}"')

(HERE / "C-gen-prefix.plan.toml").write_text("\n".join(lines) + "\n", encoding="utf-8")
print(f"programs={len(index)} cells={len(index) * 3}")

