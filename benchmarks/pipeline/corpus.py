#!/usr/bin/env python3
"""Builds the corpus of EXISTING programs for the pipeline-invariance experiment (X1a).

Everything is tagged `dev`: the programs were written by the developers of the passes under
test. The corpus has two parts.

`programs` (the agree set): programs whose zero-pass run is expected to succeed. Each is run
under every pipeline and must agree with its zero-pass run or be refused.
  1. test plans: every distinct `.cml` case referenced by a camel test that is expected to
     succeed (`expect_exit = 0`), excluding plans under parse/, format/ and perf/, tests run
     through another tool (python/node drivers), and `inspect` invocations. One entry per
     distinct environment a succeeding test gives the program (`envs = ["NAME=value", ...]`;
     the first run of this experiment read the key `env`, which the plans do not use, and
     therefore ran every program without its environment).
  2. exemplars: `exemplars/*/program.cml`, one entry per distinct environment of the runs in
     `exemplar.toml` that execute the program (no pass, or a scheduling pass only) and expect
     exit 0.
  3. benchmark models: `benchmarks/*/model.cml` with the environment `benchmarks/common/camel.py`
     sets, in two workloads: `forward` (one warm-up call, one timed call, output saved) and
     `train` (the stateful loop: one warm-up step and two timed steps, final parameters saved).
     Their timing lines are masked (see OUTPUT_MASKS in classify.py).

`must_refuse` (the refuse set): command lines that are expected to be rejected BEFORE anything
executes, by the front end (parse / typecheck) or by a pass of the pipeline.
  4. test plans: every camel test with `expect_exit != 0` (same exclusions as above), with the
     passes and environment of that test and the diagnostic the plan expects.
  5. exemplars: the runs of `exemplar.toml` that execute the program and expect a non-zero exit.
  6. the refusals listed in `test/cases/modules/onnx/pass_export.py` (REFUSALS).
Deviation D5: a command line whose plan expects a failure while the program runs (or states no
diagnostic at all) is not in this set; it is listed under `removed_from_must_refuse` with the
reason and is not run. What a plan expects is read from the plan (`expectation_kind`), never
from what the compiler did.

Deviation D4: `exclusions.json` (next to this file) lists the programs that print wall-clock
times or unseeded random values. They stay in `programs` (they are still run: the must-refuse
pipelines apply to them and their repeats keep feeding the determinism check) and carry the
reason in `exclusion`; classify.py does not judge them.
The pipelines that every program must refuse (unknown pass, `onnx::export` alone) are in
pipelines.py (EXPECT_REFUSE); they are run on the agree set.

Environment values may contain placeholders that run.py fills in for each run:
  {tmp}    a fresh directory for this run (output files of the program go there; a plan path
           `tmp/...` becomes `{tmp}/<file name>`)
  {repo}   the repository root
  {bench}  the directory holding the generated benchmark weights and inputs

Excluded programs are listed with the reason, so the numbers can be audited.

Usage: corpus.py --out corpus.json [--repo-root DIR] [--skip-benchmarks]
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pipelines as pl  # noqa: E402

EXCLUDED_PLAN_DIRS = ("parse", "format", "perf")
SCHEDULING = {"std::nvm", "std::fvm", "std::jit", "std::tf", "std::prl", "std::nodevm", "std::fastvm",
              "std::taskflow", "std::default", "std::linear", "std::parallel"}

# Environment variables the harness sets itself; a plan value for them is not part of a
# program's environment.
HARNESS_ENV: set[str] = set()  # the effect trace is a pass parameter now (H81)

# Programs whose native value of the exported function is observable in the zero-pass run, so
# the exported model can be compared with native execution (the artifact gate). The inputs
# are literals of the program; `must_contain` are the source fragments this entry relies on:
# when one is missing the entry is dropped and the program falls back to the weaker oracle.
ONNX_NATIVE = {
    "test/cases/modules/onnx/export_branch.cml": {
        "kind": "printed",
        "must_contain": ["reshape([1.0, -2.0, 3.0, 0.5], [4])",
                         "'camel gate={} {} {} {}'->format<y[0], y[1], y[2], y[3]>->println",
                         "export_model((t: Tensor): Tensor => gate(t), x,"],
        "feeds": [{"shape": [4], "values": [1.0, -2.0, 3.0, 0.5]}],
        "pattern": r"^camel gate=(\S+) (\S+) (\S+) (\S+)$",
    },
    "exemplars/08_structured_control_flow/program.cml": {
        "kind": "printed",
        "must_contain": ["reshape([1.0, -2.0, 3.0, 0.5], [4])",
                         "'camel gate={} {} {} {}'->format<y[0], y[1], y[2], y[3]>->println",
                         "export_model((t: Tensor): Tensor => gate(t), x,"],
        "feeds": [{"shape": [4], "values": [1.0, -2.0, 3.0, 0.5]}],
        "pattern": r"^camel gate=(\S+) (\S+) (\S+) (\S+)$",
    },
}

BENCH_WORKLOADS = {
    "forward": {"CAMEL_BENCH_WARMUP": "1", "CAMEL_BENCH_REPS": "1",
                "CAMEL_BENCH_OUTPUT": "{tmp}/output.npy"},
    "train": {"CAMEL_BENCH_WORKLOAD": "train", "CAMEL_BENCH_WARMUP": "1", "CAMEL_BENCH_REPS": "2",
              "CAMEL_BENCH_PARAMS": "{tmp}/params"},
}
BENCH_THREADS = {"CAMEL_BENCH_THREADS": "1", "OMP_NUM_THREADS": "1", "MKL_NUM_THREADS": "1",
                 "OPENBLAS_NUM_THREADS": "1"}


def repo_root(override: str = "") -> Path:
    value = override or os.environ.get("CAMEL_REPO_ROOT", "")
    return Path(value).resolve() if value else Path(__file__).resolve().parents[2]


def program_id(path: str) -> str:
    return re.sub(r"[^A-Za-z0-9_.-]", "_", path.removesuffix(".cml"))


_STR = re.compile(r'"((?:[^"\\]|\\.)*)"')


def parse_tables(text: str, table: str) -> dict:
    """Reads the fields this script needs from a test plan or an exemplar manifest. The plans
    are TOML-like but their multi-line `verify_script` strings are not valid TOML (the test
    runner has its own reader), so they are read line by line: `key = value` with string,
    integer and single-line string-array values; triple-quoted blocks are skipped."""
    plan: dict = {table: []}
    current = plan
    in_block = False
    block_key, block = "", []
    for line in text.splitlines():
        stripped = line.strip()
        if in_block:
            if '"""' in stripped:
                in_block = False
                current[block_key] = "\n".join(block)   # kept as text (D5 reads verify_script)
            else:
                block.append(line)
            continue
        if stripped == f"[[{table}]]":
            current = {}
            plan[table].append(current)
            continue
        m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(.*)$", stripped)
        if not m:
            continue
        key, value = m.group(1), m.group(2)
        if value.startswith('"""'):
            in_block = value.count('"""') < 2
            block_key, block = key, [value[3:]]
            if not in_block:
                current[key] = value[3:].rsplit('"""', 1)[0]
            continue
        if value.startswith("["):
            current[key] = [x.replace('\\"', '"').replace("\\\\", "\\") for x in _STR.findall(value)]
        elif value.startswith('"'):
            found = _STR.match(value)
            current[key] = found.group(1) if found else value
        elif re.fullmatch(r"-?\d+", value):
            current[key] = int(value)
        else:
            current[key] = value
    return plan


def read_vars(root: Path) -> dict:
    path = root / "test" / "vars.toml"
    out: dict = {}
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            m = re.match(r"([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\[(.*)\]\s*$", line.strip())
            if m:
                out[m.group(1)] = _STR.findall(m.group(2))
    return out


def env_of(items: list) -> dict:
    """`NAME=value` items to a dict. A relative `tmp/...` path (where the test plans put the
    files a program writes) becomes a file in the per-run directory."""
    env = {}
    for item in items:
        name, _, value = item.partition("=")
        if name in HARNESS_ENV:
            continue
        if value.startswith("tmp/"):
            value = "{tmp}/" + value.rsplit("/", 1)[-1]
        env[name] = value
    return env


def expand_args(argv: list, variables: dict):
    """Returns (passes, other): the pass names of a test's argument list after expanding the
    variables of test/vars.toml, and whatever else is on the command line."""
    passes, other = [], []
    for a in argv:
        m = re.fullmatch(r"\$\{([A-Za-z_0-9]+)\}", a)
        items = [a]
        if m and m.group(1) == "case":
            continue
        if m and m.group(1) in variables:
            items = variables[m.group(1)]
        for item in items:
            (passes if "::" in item and not item.startswith("-") and "/" not in item else other).append(item)
    # `--output-format json` is how run.py invokes camel anyway: not a difference.
    for i in range(len(other) - 1):
        if other[i] in ("--output-format", "--error-format", "-E") and other[i + 1] == "json":
            other[i:i + 2] = []
            break
    return passes, other


_DIAG_NAME = re.compile(r"\b([A-Z][A-Za-z]*Diag::[A-Za-z]+)")
_STAGE = re.compile(r"\bstage\s*={2,3}\s*['\"]([^'\"]+)['\"]")


def expectation_kind(expected_diagnostic: str, script: str) -> tuple:
    """(kind, basis): what a failing command line is expected to do, from what its plan states:
    the stage its verify script asserts (`d.data.stage === '...'`), else the diagnostic names the
    plan gives (`expect_diagnostic_name`, the manifest's `diagnostic`, names in the verify
    script). Kinds are those of pipelines.MUST_REFUSE_KINDS plus run_time, not_stated,
    ambiguous. SyntaxDiag / SemanticDiag are front-end diagnostics; of the RuntimeDiag family
    only pipelines.PASS_STAGE_DIAGNOSTICS are raised by a pass before execution."""
    stages = sorted(set(_STAGE.findall(script or "")))
    if stages:
        kinds = {"front_end" if s in ("parse", "typecheck") else "pipeline_refusal" if s.startswith("pass:")
                 else "run_time" for s in stages}
        return (kinds.pop() if len(kinds) == 1 else "ambiguous"), "stage asserted by the plan: " + ", ".join(stages)
    names = sorted(set(_DIAG_NAME.findall((expected_diagnostic or "") + " " + (script or ""))))
    if not names:
        return "not_stated", "the plan states neither a diagnostic name nor a stage"
    kinds = {"pipeline_refusal" if n in pl.PASS_STAGE_DIAGNOSTICS else "run_time" if n.startswith("RuntimeDiag::")
             else "front_end" for n in names}
    return (kinds.pop() if len(kinds) == 1 else "ambiguous"), "diagnostic named by the plan: " + ", ".join(names)


def load_exclusions(here: Path) -> dict:
    path = here / "exclusions.json"
    if not path.exists():
        return {}
    return {e["path"]: e for e in json.loads(path.read_text(encoding="utf-8")).get("excluded", [])}


def has_export_entry(source: str) -> bool:
    return bool(re.search(r"\bexport_model\s*\(", source)) and bool(re.search(r"\bonnx\b", source))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--repo-root", default="", help="repository root (default: two levels above "
                                                    "this file, or $CAMEL_REPO_ROOT)")
    ap.add_argument("--skip-benchmarks", action="store_true",
                    help="leave the benchmark models out (they are then listed as excluded)")
    args = ap.parse_args()
    root = repo_root(args.repo_root)
    variables = read_vars(root)

    programs: dict[str, dict] = {}       # key: path + canonical env
    variants: dict[str, int] = {}         # path -> number of entries
    must_refuse: list[dict] = []
    refuse_seen: set = set()
    removed: list[dict] = []
    exclusion_list = load_exclusions(Path(__file__).resolve().parent)
    excluded: list[dict] = []
    seen_excluded: set[tuple[str, str]] = set()

    def exclude(path: str, reason: str) -> None:
        if (path, reason) not in seen_excluded:
            seen_excluded.add((path, reason))
            excluded.append({"path": path, "reason": reason})

    def source_of(path: str) -> str:
        return (root / path).read_text(encoding="utf-8", errors="replace")

    def onnx_spec(path: str):
        source = source_of(path)
        if not has_export_entry(source):
            return None
        native = ONNX_NATIVE.get(path)
        if native and not all(frag in source for frag in native["must_contain"]):
            native = None
        return {"native": native}

    def add_program(path: str, env: dict, origin: str, variant_hint: str = "") -> dict:
        key = path + "\0" + json.dumps(env, sort_keys=True)
        if key not in programs:
            n = variants.get(path, 0) + 1
            variants[path] = n
            suffix = f"@{variant_hint}" if variant_hint else ("" if n == 1 else f"@v{n}")
            programs[key] = {"id": program_id(path) + suffix, "path": path, "env": env, "role": "agree",
                             "corpus": "dev", "origin": origin, "variant": variant_hint or f"v{n}",
                             "onnx": onnx_spec(path), "output_mask": "", "plans": [], "plan_passes": [],
                             "exclusion": exclusion_list.get(path, {}).get("reason", "")}
        return programs[key]

    def add_refusal(path: str, env: dict, passes: list, origin: str, source: str, expect: dict,
                    script: str = "") -> None:
        key = (path, json.dumps(env, sort_keys=True), tuple(passes))
        if key in refuse_seen:
            return
        refuse_seen.add(key)
        # The id counts every failing command line of the file, kept or removed, so that ids are
        # the same as in the runs made before D5.
        n = sum(1 for m in must_refuse + removed if m["path"] == path) + 1
        kind, basis = expectation_kind(expect.get("diagnostic", ""), script)
        entry = {"id": f"{program_id(path)}@refuse{n}", "path": path, "env": env,
                 "role": "must_refuse", "corpus": "dev", "origin": origin,
                 "passes": passes, "expect": expect, "source": source,
                 "expect_kind": kind, "expect_kind_basis": basis}
        if kind in pl.MUST_REFUSE_KINDS:
            must_refuse.append(entry)
        else:
            entry["removed_reason"] = {
                "run_time": "the plan expects a failure while the program runs, not a refusal before execution",
                "not_stated": "the plan states no diagnostic name and no stage; a refusal before execution "
                              "cannot be shown to be what it expects",
                "ambiguous": "the plan names diagnostics of more than one kind"}[kind]
            removed.append(entry)

    # 1 and 4: test plans.
    plans_root = root / "test" / "plans"
    for plan in sorted(plans_root.rglob("*.plan.toml")):
        rel_plan = plan.relative_to(plans_root).as_posix()
        data = parse_tables(plan.read_text(encoding="utf-8"), "tests")
        for test in data.get("tests", []):
            case = test.get("case", "")
            if not case.endswith(".cml"):
                continue
            path = (plan.parent / case).resolve().relative_to(root).as_posix()
            parts = rel_plan.split("/")
            if any(d in parts for d in EXCLUDED_PLAN_DIRS):
                exclude(path, f"plan directory excluded ({'/'.join(parts[:-1])})")
                continue
            if test.get("tool", "camel") != "camel":
                exclude(path, f"run through tool '{test.get('tool')}'")
                continue
            argv = [str(a) for a in test.get("args", [])]
            if argv and argv[0] in ("inspect", "check") or "${inspect" in " ".join(argv):
                exclude(path, "inspect/check invocation, not an execution")
                continue
            raw_env = list(test.get("envs", [])) + list(test.get("env", []))
            if any("${" in item for item in raw_env):
                exclude(path, "environment uses harness variables")
                continue
            env = env_of(raw_env)
            passes, other = expand_args(argv, variables)
            label = f"{data.get('id')}/{test.get('name')}"
            if test.get("expect_exit", 0) != 0:
                if other:
                    exclude(path, f"expected to fail, with command-line options ({' '.join(other)}): "
                                  f"not a pipeline")
                    continue
                expect = {"exit": test.get("expect_exit"),
                          "diagnostic": test.get("expect_diagnostic_name", ""),
                          "line": test.get("expect_diagnostic_line", ""),
                          "column": test.get("expect_diagnostic_column", "")}
                add_refusal(path, env, passes, "test/cases", f"test plan {label}", expect,
                            str(test.get("verify_script", "")))
                continue
            if passes and passes[-1] not in SCHEDULING and passes[-1].startswith("onnx::"):
                # A test of the export pass: the program is not executed, so it does not give
                # the program an execution environment.
                continue
            entry = add_program(path, env, "test/cases")
            if data.get("id") not in entry["plans"]:
                entry["plans"].append(data.get("id"))
            entry["plan_passes"].append(passes)

    # 2 and 5: exemplars.
    for manifest in sorted((root / "exemplars").glob("*/exemplar.toml")):
        prog = manifest.parent / "program.cml"
        if not prog.exists():
            continue
        path = prog.relative_to(root).as_posix()
        made = False
        for run in parse_tables(manifest.read_text(encoding="utf-8"), "runs").get("runs", []):
            argv = [str(a) for a in run.get("args", [])]
            env = env_of(list(run.get("env", [])) + list(run.get("envs", [])))
            if any(a not in SCHEDULING for a in argv):
                continue  # a graph dump or a rewrite pipeline, not the program's own run
            if run.get("exit", 0) == 0:
                entry = add_program(path, env, "exemplars")
                entry["plans"].append(f"exemplar.toml/{run.get('name')}")
                made = True
            else:
                add_refusal(path, env, argv, "exemplars", f"{path[:-11]}exemplar.toml run '{run.get('name')}'",
                            {"exit": run.get("exit"), "diagnostic": run.get("diagnostic", ""),
                             "line": run.get("diagnostic_line", ""), "column": run.get("diagnostic_column", "")})
        if not made:
            exclude(path, "exemplar has no run that executes the program and expects exit 0 "
                          "(its failing runs are in the must-refuse set)")

    # 6: the refusals the export-pass test driver checks.
    driver = root / "test" / "cases" / "modules" / "onnx" / "pass_export.py"
    if driver.exists():
        spec = importlib.util.spec_from_file_location("x1a_pass_export", driver)
        mod = importlib.util.module_from_spec(spec)
        sys.dont_write_bytecode = True  # do not leave a __pycache__ in the test tree
        try:
            spec.loader.exec_module(mod)
            for program, passes, fragment, line in mod.REFUSALS:
                path = (driver.parent / program).relative_to(root).as_posix()
                add_refusal(path, {}, list(passes), "test/cases",
                            "test/cases/modules/onnx/pass_export.py REFUSALS",
                            {"exit": 1, "diagnostic": "RuntimeDiag::TranslationRefused",
                             "line": line or "", "column": "", "message_contains": fragment})
        except Exception as e:  # noqa: BLE001 - the driver is not ours; report and go on
            exclude(driver.relative_to(root).as_posix(), f"could not read REFUSALS: {e}")

    # 3: benchmark models.
    bench_models = sorted(p.parent.name for p in (root / "benchmarks").glob("*/model.cml"))
    for model in bench_models:
        path = f"benchmarks/{model}/model.cml"
        if args.skip_benchmarks:
            exclude(path, "benchmark models skipped (--skip-benchmarks)")
            continue
        for workload, extra in BENCH_WORKLOADS.items():
            env = {"CAMEL_PACKAGES": "{repo}/benchmarks/common",
                   "CAMEL_BENCH_WEIGHTS": "{bench}/" + model + "/weights",
                   "CAMEL_BENCH_INPUT": "{bench}/" + model + "/input.npy",
                   **BENCH_THREADS, **extra}
            entry = add_program(path, env, "benchmarks", workload)
            entry["output_mask"] = "camel_bench_ms"
            entry["bench_model"] = model
            # The benchmark driver exports only on request (CAMEL_BENCH_EXPORT) and from an
            # input loaded at run time, which a pass cannot see: no export entry here.
            entry["onnx"] = None

    kept = sorted(programs.values(), key=lambda p: p["id"])
    ids = [p["id"] for p in kept] + [m["id"] for m in must_refuse + removed]
    if len(ids) != len(set(ids)):
        raise SystemExit("corpus.py: duplicate program ids")
    agree_paths = {p["path"] for p in kept}
    only_excluded = [e for e in excluded if e["path"] not in agree_paths]
    by_origin = {}
    for p in kept:
        by_origin[p["origin"]] = by_origin.get(p["origin"], 0) + 1
    out = {"label": "preliminary, container on a dev machine",
           "programs": kept, "must_refuse": sorted(must_refuse, key=lambda m: m["id"]),
           "removed_from_must_refuse": sorted(removed, key=lambda m: m["id"]),
           "excluded": only_excluded,
           "counts": {"programs": len(kept), "distinct_files": len(agree_paths),
                      "by_origin": by_origin,
                      "with_environment": sum(1 for p in kept if p["env"]),
                      "with_export_entry": sum(1 for p in kept if p["onnx"]),
                      "with_native_onnx_oracle": sum(1 for p in kept if p["onnx"] and p["onnx"]["native"]),
                      "must_refuse": len(must_refuse),
                      "must_refuse_distinct_files": len({m["path"] for m in must_refuse}),
                      "must_refuse_by_kind": {k: sum(1 for m in must_refuse if m["expect_kind"] == k)
                                              for k in sorted({m["expect_kind"] for m in must_refuse})},
                      "removed_from_must_refuse": len(removed),
                      "removed_from_must_refuse_by_kind": {k: sum(1 for m in removed if m["expect_kind"] == k)
                                                           for k in sorted({m["expect_kind"] for m in removed})},
                      "on_exclusion_list": sum(1 for p in kept if p["exclusion"]),
                      "on_exclusion_list_distinct_files": len({p["path"] for p in kept if p["exclusion"]}),
                      "excluded_programs": len({e["path"] for e in only_excluded})}}
    Path(args.out).write_text(json.dumps(out, indent=1), encoding="utf-8")
    print(json.dumps(out["counts"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
