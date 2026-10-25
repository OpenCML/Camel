"""Checks the effect trace written by `std::profile effect_trace=...`.

  order   exemplars/05_effect_order runs under NodeVM, FastVM and inline + NodeVM; the three
          traces are identical line by line once `seq` is dropped, every line names an
          effectful operator with a source origin, and stdout is the same.
  stage   a macro function with a println in its body: the println is traced with stage
          "macro" under std::macro and with stage "run" without it; the trace is otherwise
          the same.
  payload a generated program printing a short string, a string with JSON-special characters
          and one longer than the payload limit: `payload` is the hashed argument text
          (`payload_sha1` is its SHA-1), and the long one is cut on a character boundary below
          2048 bytes with `payload_truncated` set, its hash still covering the full text.

  variables a `var` declared, read and assigned: the trace has the store (`:op/assn*`) and the
          println; creating and reading the variable (`:var/new`, `:var/get`) emit nothing.

Usage: python effect_trace.py order|stage|payload|variables [--camel PATH]
"""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO_ROOT = HERE.parents[3]

sys.path.insert(0, str(REPO_ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402


# The install this case measures: `--camel <path>` names it (the author's ruling of 2026-10-25
# replaced the `CAMEL_EXE` environment switch with that argument); without it,
# `$CAMEL_HOME/bin/camel`, else this tree's `out/latest/bin/camel`. One rule, in one place:
# test/tools/camel_arg.py. `ARGV` is this process's arguments without the `--camel` pair.
CAMEL, ARGV = camel_from_argv(REPO_ROOT)


def camel_exe() -> str:
    return CAMEL


def run(program: Path, passes: list[str], trace: Path) -> tuple[int, str, list[dict]]:
    env = dict(os.environ)
    env.setdefault("CAMEL_HOME", str(Path(CAMEL).resolve().parents[1]))
    if trace.exists():
        trace.unlink()
    # The trace is a parameter of the run: std::profile turns it on and writes no report.
    profile = f'std::profile effect_trace="{trace.as_posix()}" report=false hints=false'
    proc = subprocess.run([camel_exe(), str(program), profile, *passes], env=env,
                          capture_output=True, text=True, cwd=REPO_ROOT)
    lines = ([json.loads(l) for l in trace.read_text(encoding="utf-8").splitlines()]
             if trace.exists() else [])
    return proc.returncode, proc.stdout, lines


def hashed_text(args: list[str]) -> str:
    """The text payload_sha1 covers for a call with these normal arguments and no `with` ones:
    0x1f after each argument, 0x1e between the two argument lists."""
    return "".join(a + "\x1f" for a in args) + "\x1e"


def sha1(text: str) -> str:
    return hashlib.sha1(text.encode("utf-8")).hexdigest()


def strip_seq(lines: list[dict]) -> list[dict]:
    return [{k: v for k, v in l.items() if k != "seq"} for l in lines]


def main() -> int:
    mode = ARGV[0]
    failures: list[str] = []

    def check(cond: bool, what: str) -> None:
        print(f"{'ok' if cond else 'FAIL'}: {what}")
        if not cond:
            failures.append(what)

    with tempfile.TemporaryDirectory() as t:
        trace = Path(t) / "trace.jsonl"
        if mode == "order":
            program = REPO_ROOT / "exemplars" / "05_effect_order" / "program.cml"
            pipelines = {"nvm": ["std::nvm"], "fvm": ["std::fvm"], "inline+nvm": ["std::opt::inline", "std::nvm"]}
            results = {name: run(program, passes, trace) for name, passes in pipelines.items()}
            ref_code, ref_out, ref_lines = results["nvm"]
            check(ref_code == 0, f"nvm exit code 0 (got {ref_code})")
            check(len(ref_lines) > 0, f"nvm trace has effect lines (got {len(ref_lines)})")
            check([l["seq"] for l in ref_lines] == list(range(len(ref_lines))), "seq counts from 0")
            check(all(l["uri"] == ":io/println" and l["stage"] == "run" and l["origin"]
                      and l["origin"]["file"].replace("\\", "/").endswith("05_effect_order/program.cml")
                      and len(l["payload_sha1"]) == 40 for l in ref_lines),
                  "every line: println, stage run, origin in the program, sha1 payload")
            first = ref_lines[0].get("payload") if ref_lines else None
            check(first == "string=start|||", f"first payload is the printed argument (got {first!r})")
            check(all(l.get("payload", "").endswith("|||") and "payload_truncated" not in l
                      and l["payload_sha1"] == sha1(hashed_text([l["payload"][:-3]]))
                      for l in ref_lines),
                  "every line: payload_sha1 is the SHA-1 of the payload text, nothing truncated")
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
        elif mode == "payload":
            # One ASCII letter then 1500 two-byte characters: with the "string=" prefix the
            # 2048-byte limit falls inside a character, so the cut backs off by one byte.
            long_text = "ab" + "é" * 1500
            special = 'q"b\\s|p\tz'  # quote, backslash, the separator character, a tab
            program = Path(t) / "payload.cml"
            program.write_text(
                "module effect_trace_payload\n\nfunc main(): int sync {\n"
                '    println("hi")\n'
                '    println("q\\"b\\\\s|p\\tz")\n'
                f'    println("{long_text}")\n'
                "    return 0\n}\n", encoding="utf-8")
            code, out, lines = run(program, ["std::nvm"], trace)
            check(code == 0 and len(lines) == 3, f"exit 0 and three trace lines (got {code}, {len(lines)})")
            if len(lines) == 3:
                short, spec, long = lines
                check(short.get("payload") == "string=hi|||" and "payload_truncated" not in short,
                      f"short payload is 'string=hi|||', not truncated (got {short.get('payload')!r})")
                check(short["payload_sha1"] == sha1(hashed_text(["string=hi"])),
                      "short payload_sha1 is the SHA-1 of the text with 0x1f/0x1e separators")
                check(spec.get("payload") == f"string={special}|||",
                      f"quote, backslash, '|' and tab survive JSON escaping (got {spec.get('payload')!r})")
                full = "string=" + long_text
                cut = long.get("payload", "")
                check(long.get("payload_truncated") is True, "long payload carries payload_truncated: true")
                check(len(cut.encode("utf-8")) == 2047 and full.startswith(cut),
                      f"long payload is a prefix cut on a character boundary below 2048 bytes "
                      f"(got {len(cut.encode('utf-8'))} bytes)")
                check(long["payload_sha1"] == sha1(hashed_text([full])),
                      "long payload_sha1 covers the full text")
        elif mode == "variables":
            program = Path(t) / "variables.cml"
            program.write_text(
                "module effect_trace_variables\n\nfunc main(): int sync {\n"
                "    var c = 1\n"
                "    let a = c + 1\n"
                "    c += a\n"
                "    println(c)\n"
                "    return 0\n}\n", encoding="utf-8")
            for passes in (["std::nvm"], ["std::fvm"], ["std::opt::inline", "std::nvm"]):
                code, out, lines = run(program, passes, trace)
                uris = [l["uri"] for l in lines]
                check(code == 0 and out == "3\n", f"{passes}: prints 3")
                check(uris == [":op/assn_add_l", ":io/println"],
                      f"{passes}: the store and the println are traced, :var/new and :var/get "
                      f"are not (got {uris})")
        else:
            raise SystemExit(f"unknown mode: {mode}")

    print("effect trace: " + ("PASS" if not failures else "FAIL"))
    return 0 if not failures else 1


if __name__ == "__main__":
    sys.exit(main())
