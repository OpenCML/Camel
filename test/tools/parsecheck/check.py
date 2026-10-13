#!/usr/bin/env python3
"""The corpus checks of the front end, as the test plan runs them.

    python3 test/tools/parsecheck/check.py snapshot [--update]
    python3 test/tools/parsecheck/check.py format
    python3 test/tools/parsecheck/check.py fuzz [ITERATIONS [SEED]]

snapshot  Every .cml file of the repository and every probe (probes.py) is parsed; a hash of the
          normalized AST of each (or of its diagnostics, when it is rejected) is compared with
          test/plans/feat/parse/golden/ast_hashes.txt. The golden list was recorded when the
          hand-written parser was last compared, tree for tree, with the ANTLR parser it
          replaced; a line that changes means the parser reads that file differently.
          --update rewrites the list (say in the commit why the reading changed).
format    Formatting every accepted file gives a text with the same AST.
fuzz      Mutated programs: the parser returns a lossless tree for each (camel-parsecheck fuzz).

Run from the repository root; the tools are those of out/latest unless CAMEL_HOME is set.
"""
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
GOLDEN = ROOT / "test" / "plans" / "feat" / "parse" / "golden" / "ast_hashes.txt"

sys.path.insert(0, str(HERE))
import corpus  # noqa: E402
import probes  # noqa: E402


def tool():
    home = Path(os.environ.get("CAMEL_HOME", ROOT / "out" / "latest"))
    name = "camel-parsecheck.exe" if os.name == "nt" else "camel-parsecheck"
    return str(home / "bin" / name)


def run(args):
    proc = subprocess.run([tool(), *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    return proc.returncode, proc.stdout, proc.stderr


def repository_files():
    os.chdir(ROOT)
    return [str(p).replace("\\", "/") for p in corpus.walk(Path("."), ".cml")]


def write_list(folder, files):
    path = Path(folder) / "list.txt"
    path.write_text("\n".join(files) + "\n", encoding="utf-8", newline="\n")
    return str(path)


def snapshot(update):
    with tempfile.TemporaryDirectory() as tmp:
        sys.argv = ["probes.py", tmp]
        probes.main()
        probe_files = sorted(str(p).replace("\\", "/") for p in Path(tmp).glob("probe_*.cml"))
        code, out, err = run(["hashes", write_list(tmp, repository_files() + probe_files)])
        if code != 0:
            print(err or out)
            return 1
        prefix = str(Path(tmp)).replace("\\", "/") + "/"
        lines = [line.replace(prefix, "probes/") for line in out.splitlines()]
    text = "\n".join(lines) + "\n"
    if update:
        GOLDEN.write_text(text, encoding="utf-8", newline="\n")
        print(f"wrote {GOLDEN.relative_to(ROOT)} ({len(lines)} files)")
        return 0
    golden = GOLDEN.read_text(encoding="utf-8").splitlines() if GOLDEN.exists() else []
    was = {line.split(" ", 2)[2]: line for line in golden}
    now = {line.split(" ", 2)[2]: line for line in lines}
    bad = 0
    added = 0
    for path, line in now.items():
        if path not in was:
            added += 1  # a new test case: recorded at the next --update
        elif was[path] != line:
            print(f"read differently: {path}\n    was {was[path][:19]}\n    now {line[:19]}")
            bad += 1
    if bad:
        print(f"{bad} difference(s). If the change is intended: python3 test/tools/parsecheck/check.py snapshot --update")
        return 1
    print(f"ast snapshot: {len(lines) - added} files read as recorded, {added} not in the snapshot yet")
    return 0


def format_check():
    with tempfile.TemporaryDirectory() as tmp:
        code, out, err = run(["fmtcheck", write_list(tmp, repository_files())])
    print((out + err).strip())
    return code


def fuzz(iterations, seed):
    with tempfile.TemporaryDirectory() as tmp:
        code, out, err = run(["fuzz", write_list(tmp, repository_files()), str(iterations), str(seed), tmp])
        print((out + err).strip())
        if code != 0:
            for path in sorted(Path(tmp).glob("fail_*.cml"))[:3]:
                print(f"---- {path.name}\n{path.read_text(encoding='utf-8', errors='replace')[:1500]}")
    return code


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else ""
    if mode == "snapshot":
        return snapshot("--update" in sys.argv[2:])
    if mode == "format":
        return format_check()
    if mode == "fuzz":
        return fuzz(int(sys.argv[2]) if len(sys.argv) > 2 else 3000, int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main())
