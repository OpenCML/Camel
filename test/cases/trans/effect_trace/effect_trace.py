"""Checks the effect trace written under CAMEL_EFFECT_TRACE.

  order   exemplars/05_effect_order runs under NodeVM, FastVM and inline + NodeVM; the three
          traces are identical line by line once `seq` is dropped, every line names an
          effectful operator with a source origin, and stdout is the same.
  stage   a macro function with a println in its body: the println is traced with stage
          "macro" under std::macro and with stage "run" without it; the trace is otherwise
          the same.

Usage: python effect_trace.py order|stage   (needs only the standard library)
"""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]


def camel_exe() -> str:
    override = os.environ.get("CAMEL_EXE")
    if override:
        return override
    return str(REPO_ROOT / "out" / "latest" / "bin" / ("camel.exe" if os.name == "nt" else "camel"))


def run(program: Path, passes: list[str], trace: Path) -> tuple[int, str, list[dict]]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(REPO_ROOT / "out" / "latest"))
    env["CAMEL_EFFECT_TRACE"] = str(trace)
    if trace.exists():
        trace.unlink()
    proc = subprocess.run([camel_exe(), str(program), *passes], env=env, capture_output=True,
                          text=True, cwd=REPO_ROOT)
    lines = [json.loads(l) for l in trace.read_text().splitlines()] if trace.exists() else []
    return proc.returncode, proc.stdout, lines


def strip_seq(lines: list[dict]) -> list[dict]:
    return [{k: v for k, v in l.items() if k != "seq"} for l in lines]


def main() -> int:
    mode = sys.argv[1]
    failures: list[str] = []

    def check(cond: bool, what: str) -> None:
        print(f"{'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as t:
        trace = Path(t) / "trace.jsonl"
        if mode == "order":
            program = REPO_ROOT / "exemplars" / "05_effect_order" / "program.cml"
            pipelines = {"nvm": ["std::nvm"], "fvm": ["std::fvm"], "inline+nvm": ["std::inline", "std::nvm"]}
            results = {name: run(program, passes, trace) for name, passes in pipelines.items()}
            ref_code, ref_out, ref_lines = results["nvm"]
            check(ref_code == 0, f"nvm exit code 0 (got {ref_code})")
            check(len(ref_lines) > 0, f"nvm trace has effect lines (got {len(ref_lines)})")
            check([l["seq"] for l in ref_lines] == list(range(len(ref_lines))), "seq counts from 0")
            check(all(l["uri"] == ":io/println" and l["stage"] == "run" and l["origin"]
                      and l["origin"]["file"].replace("\\", "/").endswith("05_effect_order/program.cml")
                      and len(l["payload_sha1"]) == 40 for l in ref_lines),
                  "every line: println, stage run, origin in the program, sha1 payload")
            for name, (code, out, lines) in results.items():
                check(code == ref_code and out == ref_out, f"{name}: same exit code and stdout as nvm")
                check(strip_seq(lines) == strip_seq(ref_lines), f"{name}: same trace as nvm ({len(lines)} lines)")
        elif mode == "stage":
            program = HERE / "macro_effect.cml"
            code_r, out_r, run_lines = run(program, ["std::nvm"], trace)
            code_m, out_m, macro_lines = run(program, ["std::macro", "std::nvm"], trace)
            check(code_r == 0 and code_m == 0, "both pipelines exit 0")
            check(out_r == out_m == "in macro body\n42\n", "both pipelines print the same two lines")
            check([l["stage"] for l in run_lines] == ["run", "run"], "without std::macro: stages run, run")
            check([l["stage"] for l in macro_lines] == ["macro", "run"], "with std::macro: stages macro, run")
            drop = lambda ls: [{k: v for k, v in l.items() if k not in ("seq", "stage")} for l in ls]
            check(drop(run_lines) == drop(macro_lines), "traces agree on uri, origin and payload")
            check(run_lines[0]["origin"]["line"] == 8, "the macro body println is at line 8")
        else:
            raise SystemExit(f"unknown mode: {mode}")

    print("effect trace: " + ("PASS" if not failures else "FAIL"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
