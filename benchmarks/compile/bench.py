#!/usr/bin/env python3
"""Where the time and the memory of a Camel run go, phase by phase.

    python3 benchmarks/compile/bench.py run [--out DIR] [--rounds N] [--cpu K] [--sets LIST]
                                            [--camel LABEL=HOME ...] [--modules LIST]
    python3 benchmarks/compile/bench.py table DIR        render DIR/results.json again
    python3 benchmarks/compile/bench.py check            the smoke run of the test plan

`run` measures a fixed set of inputs (see INPUT SETS below) and writes DIR/results.json and
DIR/results.md (default DIR: benchmarks/compile/out, which is not tracked).

What is measured, and how:

  * The phases of a run come from `camel --time-phases` (camel/utils/phase_timer.h): start-up,
    parse, GCT, GIR, the encoding of the runtime graph, each pass, teardown; one entry per
    module and per pass. Phases nest, so the tables show the *self* time of each kind of
    phase: its duration minus that of the phases inside it. `(process)` is what the phases
    do not cover: the dynamic loader and static initialisers before main and the exit of
    the process after it (wall time of the child seen from here, minus the report's total).
  * The split of `parse` into lexer, CST and AST builder comes from `camel-parsecheck bench`,
    which times the three in-process on the same file.
  * Heap allocations (count and bytes per phase, peak live bytes) come from one extra run
    under the counting shim test/tools/allocshim, built here with the C compiler found on
    PATH. That run is not timed.
  * The peak resident set size is the one the phase report reads from /proc/self/status.

Method. Every (input, pipeline) is run `--rounds` times, pinned with `taskset -c K` when
taskset exists; when several compilers are compared (`--camel base=/path/one --camel
new=/path/two`, each a CAMEL_HOME) they alternate inside each round, so a change of load hits
both. The tables give the median and the minimum over the rounds. /proc/loadavg is recorded
before every round and reported: on a busy machine read the minimum, not the median. A case
whose single run takes longer than five seconds is run twice only, whatever --rounds says.

INPUT SETS (`--sets small,medium,large,modules`; default all):

  small    hello.cml and a ten-line arithmetic program
  medium   real programs of the repository: the test of stdlib/array.cml (which imports that
           module), the two largest benchmark models (compiled only), a progen program of the
           default size
  large    a random program of test/tools/progen at --size 20 (about 2,300 lines, every
           construct of the generator but `macro` functions) and 4,000 small functions of test/tools/scale in the
           shape `mixed` (about 14,000 lines, flat)
  modules  generated projects of N small modules (gen_modules.py) for N in --modules
           (default 1,2,4,8,16,32,64,128), in the shapes flat, chain and fan

Pipelines per input: `compile` (`std::null`: everything up to the graph, nothing executed);
for inputs that can run, `passes` (std::macro, the four std::opt sub-passes, std::opt::inline,
std::opt::specialize, then std::null), and the back ends std::nvm, std::fvm, std::jit, and
std::bc (bytecode compiled and printed, not executed: an upper bound of what std::fvm
spends before it executes).

Only Linux is supported (taskset, LD_PRELOAD, getrusage); elsewhere `run` works without
pinning and without allocation counts.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Dict, List, Optional, Tuple

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(REPO / "test" / "tools" / "progen"))
sys.path.insert(0, str(REPO / "test" / "tools" / "scale"))

import gen_modules  # noqa: E402

PASS_PIPELINE = ["std::macro", "std::opt::fold", "std::opt::cse", "std::opt::dce", "std::opt",
                 "std::opt::inline", "std::opt::specialize", "std::null"]
PIPELINES = {
    "compile": ["std::null"],
    "passes": PASS_PIPELINE,
    "nvm": ["std::nvm"],
    "fvm": ["std::fvm"],
    "jit": ["std::jit"],
    "bc": ["std::bc"],
}
RUN_PIPELINES = ["compile", "passes", "nvm", "fvm", "jit", "bc"]
DEFAULT_MODULE_COUNTS = [1, 2, 4, 8, 16, 32, 64, 128]
LARGE_SEED, LARGE_SIZE = 2, 20  # the generator rarely finds a usable program above size 20
SCALE_FUNCTIONS = 4000
SLOW_CASE_MS, SLOW_CASE_ROUNDS = 5000.0, 2  # a run longer than this is repeated only this often

# The library modules a `passes` run compiles on demand, in the order they are loaded. `std::opt`'s
# algebra step compiles stdlib/rules.cml, and `rules` imports `graph` and `math`; a module whose
# declaration is written in Camel (stdlib/graph.cml since H141, stdlib/math.cml since H112) is
# compiled when it is loaded, so it is counted here. The lazy import is the real cost, not a
# defect: they are library modules, so the program's own module count stays what the input set
# says. This pin is allowed to change when a library is restructured (a new/moved import in
# stdlib/rules.cml or a declaration module that appears or goes away): that is exactly the
# regression the pin makes visible, and the message says which modules were counted.
PASS_LIBRARY_MODULES = ("rules", "graph", "math")

# The order of the rows of the phase tables; phases not listed follow alphabetically.
PHASE_ORDER = ["(process)", "startup", "mm_init", "context", "builtin_module", "parse", "resolve", "cmo_load",
               "import", "compile", "gct", "gir", "encode"]

SMALL_ARITH = """module main

func square(x: int): int {
    return x * x
}

func main(): int sync {
    let a = square(3) + square(4)
    println(a)
    return 0
}
"""


# ---- inputs -------------------------------------------------------------------------------

def progen_program(seed: int, size: int, features: Optional[str] = None) -> Tuple[str, dict]:
    """The source of a random program and the environment variables it reads."""
    from generate import generate, parse_features
    from model import emit
    prog, _expected, _attempts = generate(seed, parse_features(features), size=size)
    return emit(prog), {k: str(v) for k, v in prog.a["env"].items()}


def build_inputs(sets: List[str], work: Path, module_counts: List[int], large_size: int) -> List[dict]:
    """Each input: id, set, path, runnable (may be executed), env, optional modules/shape."""
    inputs: List[dict] = []

    def add(ident, group, path, runnable, env=None, **extra):
        inputs.append({"id": ident, "set": group, "path": str(path), "runnable": runnable, "env": env or {}, **extra})

    if "small" in sets:
        add("hello", "small", REPO / "hello.cml", True)
        arith = work / "small_arith.cml"
        arith.write_text(SMALL_ARITH, encoding="utf-8", newline="\n")
        add("arith10", "small", arith, True)
    if "medium" in sets:
        # Programs of the repository. array_basic is the test of stdlib/array.cml (the largest
        # module of the standard library written in Camel, all generic functions) and imports
        # it; the two models import the benchmark driver module and need data files to run,
        # so they are compiled only.
        packages = {"CAMEL_PACKAGES": str(REPO / "benchmarks" / "common")}
        add("array_basic", "medium", REPO / "test" / "cases" / "modules" / "stdlib" / "array_basic.cml", True)
        add("resnetbn", "medium", REPO / "benchmarks" / "resnetbn" / "model.cml", False, packages)
        add("lora_mlp", "medium", REPO / "benchmarks" / "lora" / "lora_mlp.cml", False, packages)
        source, env = progen_program(7, 1)
        default = work / "progen_s1.cml"
        default.write_text(source, encoding="utf-8", newline="\n")
        add("progen-1", "medium", default, True, env)
    if "large" in sets:
        # Without `macro` functions: at this size std::macro meets one it cannot evaluate
        # (an operator that is unavailable ahead of time) and rejects the program.
        source, env = progen_program(LARGE_SEED, large_size, "-macro")
        large = work / f"progen_s{large_size}.cml"
        large.write_text(source, encoding="utf-8", newline="\n")
        add(f"progen-{large_size}", "large", large, True, env)
        import scale
        source, _expected = scale.program("mixed", SCALE_FUNCTIONS)
        flat = work / f"scale_mixed_{SCALE_FUNCTIONS}.cml"
        flat.write_text(source, encoding="utf-8", newline="\n")
        add(f"scale-{SCALE_FUNCTIONS}", "large", flat, True)
    if "modules" in sets:
        for shape in gen_modules.SHAPES:
            for count in module_counts:
                project = work / f"modules_{shape}_{count}"
                gen_modules.generate(project, count, shape, funcs=8, fan=4)
                # The project's own modules, by name: `main` plus the N generated ones. Anything
                # else the run compiles is a library module (see module_names()).
                own = ["main"] + [gen_modules.module_name(i) for i in range(count)]
                add(f"{shape}-{count}", "modules", project / "main.cml", True, modules=count,
                    shape=shape, own_modules=own)
    for item in inputs:
        text = Path(item["path"]).read_bytes()
        item["bytes"] = len(text)
        item["lines"] = text.count(b"\n")
        if item["set"] == "modules":
            files = list(Path(item["path"]).parent.glob("bm_*.cml"))
            item["bytes"] += sum(f.stat().st_size for f in files)
            item["lines"] += sum(f.read_bytes().count(b"\n") for f in files)
    return inputs


# ---- running ------------------------------------------------------------------------------

def loadavg() -> Optional[List[float]]:
    try:
        return [float(x) for x in Path("/proc/loadavg").read_text().split()[:3]]
    except OSError:
        return None


def build_shim(work: Path) -> Optional[Path]:
    """The counting allocator shim as a shared object, or None where it cannot be built."""
    if platform.system() != "Linux":
        return None
    compiler = next((c for c in ("cc", "gcc", "clang") if shutil.which(c)), None)
    if compiler is None:
        return None
    out = work / "allocshim.so"
    source = REPO / "test" / "tools" / "allocshim" / "allocshim.c"
    done = subprocess.run([compiler, "-O2", "-fPIC", "-shared", "-o", str(out), str(source)],
                          capture_output=True, text=True)
    if done.returncode != 0:
        print(f"bench: the allocator shim did not build, no allocation counts:\n{done.stderr}", file=sys.stderr)
        return None
    return out


def tool_path(home: Path, name: str) -> Path:
    """An executable of the installation at `home` (`camel`, `camel-parsecheck`)."""
    exe = home / "bin" / (name + ".exe")
    return exe if exe.exists() else home / "bin" / name


def camel_env(home: Path, extra: dict) -> dict:
    env = dict(os.environ)
    env["CAMEL_HOME"] = str(home)
    env.pop("CAMEL_TIME_PHASES", None)
    env.update(extra)
    return env


def run_camel(home: Path, source: str, passes: List[str], env: dict, cpu: Optional[int], report: Path,
              preload: Optional[Path] = None, timeout: float = 600.0) -> Tuple[float, int, dict]:
    """One run. Returns (wall seconds of the child, exit code, phase report)."""
    command = [str(tool_path(home, "camel")), f"--time-phases={report}", source] + passes
    if cpu is not None and shutil.which("taskset"):
        command = ["taskset", "-c", str(cpu)] + command
    if preload is not None:
        env = dict(env, LD_PRELOAD=str(preload))
    if report.exists():
        report.unlink()
    start = time.perf_counter()
    done = subprocess.run(command, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, timeout=timeout,
                          cwd=str(Path(source).parent))
    wall = time.perf_counter() - start
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {}
    if done.returncode != 0 and not data:
        sys.stderr.write(done.stderr.decode("utf-8", "replace")[-2000:])
    return wall, done.returncode, data


def self_costs(report: dict) -> Dict[str, Dict[str, float]]:
    """Phase kind -> self wall ns / allocations / bytes: each phase minus the phases inside it."""
    phases = report.get("phases", [])
    out: Dict[str, Dict[str, float]] = {}
    stack: List[int] = []
    inner = [dict(wall_ns=0, allocations=0, alloc_bytes=0) for _ in phases]
    for index, phase in enumerate(phases):
        del stack[phase["depth"]:]
        if stack:
            parent = inner[stack[-1]]
            for key in parent:
                parent[key] += phase.get(key, 0)
        stack.append(index)
    for phase, nested in zip(phases, inner):
        kind = phase["name"] + (":" + phase["detail"] if phase["name"] == "pass" else "")
        row = out.setdefault(kind, dict(wall_ns=0, allocations=0, alloc_bytes=0, count=0))
        row["count"] += 1
        for key in nested:
            row[key] += phase.get(key, 0) - nested[key]
    return out


def module_names(report: dict) -> List[str]:
    """Which modules a run compiled, in the order they first reached GCT.

    Every module compiled from Camel source opens one `gct` phase whose detail is the module's
    name (src/core/module/userdef.cpp), and the phase report keeps that name, so the list is
    available without a counter of its own. It is exactly what `counters.modules` counts — and
    that count includes library modules a pass compiles on demand: since H100 the algebra step
    of `std::opt` compiles stdlib/rules.cml, and its `import math` also loads the declaration
    module stdlib/math.cml, which is written in Camel and compiled too (H112, H121). The count
    alone cannot tell the program's own modules from the libraries it caused to be loaded; the
    names can, and those are two different cost numbers.
    """
    names: List[str] = []
    for phase in report.get("phases", []):
        if phase.get("name") == "gct":
            name = phase.get("detail") or ""
            if name and name not in names:
                names.append(name)
    return names


def module_split(entry: dict) -> Optional[Tuple[int, int]]:
    """(user, total) module counts of a results entry, or None for a file written before the
    phase report's module names were kept (rendering it must not claim 0 / 0)."""
    if "modules_loaded" not in entry:
        return None
    return len(entry.get("modules_user", [])), len(entry["modules_loaded"])


def entry_module(report: dict) -> Optional[str]:
    """The name of the program's entry module, from the `gir` phase.

    `gir` is opened only by the program's own build (src/core/module/userdef.cpp); a module built
    as a library for a pass opens `library` instead. The entry module is the one module every
    program has, so it anchors the split when the benchmark does not know the project's names.
    """
    for phase in report.get("phases", []):
        if phase.get("name") == "gir" and phase.get("detail"):
            return phase["detail"]
    return None


def classify_modules(item: dict, report: dict) -> Tuple[List[str], List[str], List[str]]:
    """(loaded, own, library) modules of one run.

    A generated project declares its own module names (`own_modules`); for a single-file program
    the program's own module is its entry module. Every other compiled module is a library
    module — loaded because a pass or the program imports it, not part of what was measured.
    """
    loaded = module_names(report)
    own_names = item.get("own_modules")
    if own_names is None:
        entry = entry_module(report)
        own_names = [entry] if entry else list(loaded)
    own_set = set(own_names)
    own = [name for name in loaded if name in own_set]
    library = [name for name in loaded if name not in own_set]
    return loaded, own, library


def parse_bench(home: Path, source: str, cpu: Optional[int], rounds: int) -> Optional[dict]:
    """Lexer / CST / AST split of one file in microseconds (min and median)."""
    tool = tool_path(home, "camel-parsecheck")
    if not tool.exists():
        return None
    command = [str(tool), "bench", source, str(rounds)]
    if cpu is not None and shutil.which("taskset"):
        command = ["taskset", "-c", str(cpu)] + command
    done = subprocess.run(command, env=camel_env(home, {}), capture_output=True, text=True)
    if done.returncode != 0:
        return None
    out: dict = {}
    head = re.search(r"tokens=(\d+) cst-nodes=(\d+)", done.stdout)
    if head:
        out["tokens"], out["cst_nodes"] = int(head.group(1)), int(head.group(2))
    for name, low, mid in re.findall(r"^\s+([a-z+]+): min=(\d+)us median=(\d+)us", done.stdout, re.M):
        out[name] = {"min_us": int(low), "median_us": int(mid)}
    return out


def summarize(samples: List[float]) -> dict:
    return {"median": statistics.median(samples), "min": min(samples), "n": len(samples)}


def measure(inputs: List[dict], versions: List[Tuple[str, Path]], rounds: int, cpu: Optional[int], work: Path,
            pipelines: List[str], shim: Optional[Path]) -> dict:
    cases = []
    for item in inputs:
        for pipeline in pipelines:
            if pipeline != "compile" and not item["runnable"]:
                continue
            for label, home in versions:
                cases.append({"input": item["id"], "pipeline": pipeline, "version": label, "_item": item,
                              "_home": home, "_wall": [], "_total": [], "_phases": {}, "_rss": []})
    loads = [loadavg()]
    report = work / "phases.json"
    for round_index in range(rounds):
        for case in cases:
            if round_index >= SLOW_CASE_ROUNDS and case["_wall"] and max(case["_wall"]) > SLOW_CASE_MS:
                continue
            item = case["_item"]
            env = camel_env(case["_home"], item["env"])
            wall, code, data = run_camel(case["_home"], item["path"], PIPELINES[case["pipeline"]], env, cpu, report)
            if code != 0 or not data:
                case["error"] = f"exit {code}"
                continue
            case["_wall"].append(wall * 1e3)
            case["_total"].append(data["total_ns"] / 1e6)
            case["_rss"].append(data.get("peak_rss_kb", 0))
            for kind, row in self_costs(data).items():
                case["_phases"].setdefault(kind, []).append(row["wall_ns"] / 1e6)
            case["_phases"].setdefault("(process)", []).append(wall * 1e3 - data["total_ns"] / 1e6)
            case["counters"] = data.get("counters", {})
            loaded, own, library = classify_modules(item, data)
            case["modules_loaded"] = loaded
            case["modules_user"] = own
            case["modules_library"] = library
            case["modules_entry"] = entry_module(data)
        loads.append(loadavg())
        print(f"bench: round {round_index + 1}/{rounds} done, load {loads[-1]}", file=sys.stderr)
    results = []
    parse_cache: Dict[Tuple[str, str], Optional[dict]] = {}
    for case in cases:
        item, home = case["_item"], case["_home"]
        entry = {k: v for k, v in case.items() if not k.startswith("_")}
        entry.update({k: item[k] for k in ("set", "lines", "bytes") if k in item})
        for key in ("modules", "shape"):
            if key in item:
                entry[key] = item[key]
        if case["_wall"]:
            entry["wall_ms"] = summarize(case["_wall"])
            entry["in_process_ms"] = summarize(case["_total"])
            entry["peak_rss_kb"] = int(statistics.median(case["_rss"]))
            entry["phases_ms"] = {kind: summarize(values) for kind, values in case["_phases"].items()}
            if shim is not None:
                _, code, data = run_camel(home, item["path"], PIPELINES[case["pipeline"]],
                                          camel_env(home, item["env"]), cpu, report, preload=shim)
                if code == 0 and data.get("alloc_shim"):
                    entry["alloc"] = data["alloc"]
                    entry["phases_alloc"] = {kind: {"allocations": row["allocations"], "bytes": row["alloc_bytes"]}
                                             for kind, row in self_costs(data).items()}
            if case["pipeline"] == "compile" and item["set"] != "modules":
                key = (case["version"], item["path"])
                if key not in parse_cache:
                    parse_cache[key] = parse_bench(home, item["path"], cpu, max(rounds, 5))
                if parse_cache[key]:
                    entry["parse_split"] = parse_cache[key]
        results.append(entry)
    return {
        "meta": {
            "date": time.strftime("%Y-%m-%d %H:%M:%S"),
            "machine": platform.platform(),
            "cpus": os.cpu_count(),
            "pinned_cpu": cpu if shutil.which("taskset") else None,
            "rounds": rounds,
            "loadavg": loads,
            "versions": {label: str(home) for label, home in versions},
            "alloc_shim": shim is not None,
        },
        "results": results,
    }


# ---- tables -------------------------------------------------------------------------------

def phase_rank(kind: str) -> Tuple[int, str]:
    if kind in PHASE_ORDER:
        return PHASE_ORDER.index(kind), kind
    if kind.startswith("pass:"):
        return len(PHASE_ORDER), kind
    return len(PHASE_ORDER) + (2 if kind == "teardown" else 1), kind


def fmt_ms(value: float) -> str:
    return f"{value:.2f}" if value < 100 else f"{value:.0f}"


def fmt_count(value: float) -> str:
    if value >= 1e6:
        return f"{value / 1e6:.2f}M"
    if value >= 1e4:
        return f"{value / 1e3:.0f}k"
    return str(int(value))


def table(header: List[str], rows: List[List[str]]) -> str:
    lines = ["| " + " | ".join(header) + " |", "|" + "|".join(["---"] + ["---:"] * (len(header) - 1)) + "|"]
    lines += ["| " + " | ".join(row) + " |" for row in rows]
    return "\n".join(lines) + "\n"


def pick(results: List[dict], **want) -> List[dict]:
    return [r for r in results if all(r.get(k) == v for k, v in want.items()) and "wall_ms" in r]


def phase_table(columns: List[dict], stat: str) -> str:
    kinds = sorted({k for r in columns for k in r["phases_ms"]}, key=phase_rank)
    rows = [[k] + [fmt_ms(r["phases_ms"][k][stat]) if k in r["phases_ms"] else "" for r in columns] for k in kinds]
    rows.append(["**wall**"] + [f"**{fmt_ms(r['wall_ms'][stat])}**" for r in columns])
    rows.append(["peak RSS (MB)"] + [f"{r['peak_rss_kb'] / 1024:.1f}" for r in columns])
    # Both module numbers: the program's own modules are what the input set varies, and the
    # library modules are what the pipeline cost on top of it. Neither may disappear.
    split = [module_split(r) for r in columns]
    rows.append(["modules (user / total)"] + ["—" if s is None else f"{s[0]} / {s[1]}" for s in split])
    rows.append(["library modules"] + [
        "—" if "modules_library" not in r else (", ".join(r["modules_library"]) or "—") for r in columns])
    return table(["phase (self, ms)"] + [f"{r['input']} ({r['lines']} lines)" for r in columns], rows)


def alloc_table(columns: List[dict]) -> str:
    columns = [r for r in columns if "phases_alloc" in r]
    if not columns:
        return "(no allocation counts: the shim was not available)\n"
    kinds = sorted({k for r in columns for k in r["phases_alloc"]}, key=phase_rank)
    rows = []
    for kind in kinds:
        row = [kind]
        for r in columns:
            cell = r["phases_alloc"].get(kind)
            row.append(f"{fmt_count(cell['allocations'])} / {fmt_count(cell['bytes'])}B" if cell else "")
        rows.append(row)
    rows.append(["**total**"] + [f"**{fmt_count(r['alloc']['allocations'])} / {fmt_count(r['alloc']['bytes'])}B**"
                                 for r in columns])
    rows.append(["peak live heap"] + [f"{fmt_count(r['alloc']['peak_bytes'])}B" for r in columns])
    for name in ("modules", "ast.nodes", "gct.nodes", "graph.nodes"):
        rows.append([name] + [fmt_count(r.get("counters", {}).get(name, 0)) for r in columns])
    return table(["phase (allocations / bytes)"] + [r["input"] for r in columns], rows)


def render(data: dict) -> str:
    meta, results = data["meta"], data["results"]
    labels = list(meta["versions"])
    first = labels[0]
    loads = [l[0] for l in meta["loadavg"] if l]
    out = ["# Compile pipeline benchmark", ""]
    out.append(f"{meta['date']}, {meta['machine']}, {meta['cpus']} logical CPUs, "
               + (f"pinned to CPU {meta['pinned_cpu']}" if meta["pinned_cpu"] is not None else "not pinned")
               + f", {meta['rounds']} rounds per case"
               + (f", 1-minute load between {min(loads):.1f} and {max(loads):.1f}." if loads else "."))
    out.append("Times are milliseconds; phase rows are self times (the phases inside a phase are taken out).")
    out.append("")
    for label in labels:
        heading = f" ({label})" if len(labels) > 1 else ""
        compile_cols = [r for r in pick(results, pipeline="compile", version=label) if r["set"] != "modules"]
        modules_big = [r for r in pick(results, pipeline="compile", version=label, set="modules")
                       if r["modules"] == max(x["modules"] for x in results if x.get("set") == "modules")]
        columns = compile_cols + modules_big
        if columns:
            out += [f"## Compile only (`std::null`){heading}: median", "", phase_table(columns, "median")]
            out += [f"## Compile only{heading}: minimum", "", phase_table(columns, "min")]
            out += [f"## Heap allocations, compile only{heading}", "", alloc_table(columns)]
            split = [r for r in columns if "parse_split" in r]
            if split:
                rows = []
                for r in split:
                    s = r["parse_split"]
                    lex, cst, ast = (s.get(k, {}).get("min_us", 0) for k in ("lex", "lex+cst", "lex+cst+ast"))
                    rows.append([r["input"], str(s.get("tokens", "")), str(s.get("cst_nodes", "")), str(lex),
                                 str(cst - lex), str(ast - cst)])
                out += [f"## Inside `parse`{heading} (camel-parsecheck bench, minimum, microseconds)", "",
                        table(["input", "tokens", "CST nodes", "lexer", "CST", "AST builder"], rows)]
        for pipeline in ("passes", "nvm", "fvm", "jit", "bc"):
            columns = [r for r in pick(results, pipeline=pipeline, version=label)
                       if r["set"] != "modules" or r in pick(results, shape="chain", modules=max(
                           [x.get("modules", 0) for x in results]))]
            if columns:
                out += [f"## Pipeline `{' '.join(PIPELINES[pipeline])}`{heading}: median", "",
                        phase_table(columns, "median")]
        scaling = pick(results, pipeline="compile", version=label, set="modules")
        if scaling:
            out += [f"## Modules: compile time against the number of modules{heading}", ""]
            for shape in gen_modules.SHAPES:
                rows = []
                for r in sorted((x for x in scaling if x["shape"] == shape), key=lambda x: x["modules"]):
                    p = r["phases_ms"]
                    cell = lambda k: fmt_ms(p[k]["min"]) if k in p else ""  # noqa: E731
                    alloc = fmt_count(r["alloc"]["allocations"]) if "alloc" in r else ""
                    split = module_split(r)
                    rows.append([str(r["modules"]), "—" if split is None else str(split[0]),
                                 "—" if split is None else str(split[1]), fmt_ms(r["wall_ms"]["min"]),
                                 fmt_ms(r["wall_ms"]["median"]), cell("resolve"), cell("parse"), cell("gct"),
                                 cell("gir"), cell("import"), cell("encode"), cell("teardown"), alloc,
                                 f"{r['peak_rss_kb'] / 1024:.1f}"])
                out += [f"Shape `{shape}` (minimum over the rounds unless stated):", "",
                        table(["modules", "user", "total", "wall min", "wall median", "resolve", "parse", "gct",
                               "gir", "import", "encode", "teardown", "allocations", "RSS MB"], rows)]
    if len(labels) > 1:
        out += [f"## Versions compared with `{first}` (wall and compile phases, minimum)", ""]
        rows = []
        for base in pick(results, version=first):
            row = [f"{base['input']} / {base['pipeline']}"]
            for label in labels:
                other = pick(results, input=base["input"], pipeline=base["pipeline"], version=label)
                if not other:
                    row += ["", "", ""]
                    continue
                o = other[0]
                gct = o["phases_ms"].get("gct", {}).get("min", 0.0)
                gir = o["phases_ms"].get("gir", {}).get("min", 0.0)
                alloc = fmt_count(o["alloc"]["allocations"]) if "alloc" in o else ""
                row += [fmt_ms(o["wall_ms"]["min"]), f"{fmt_ms(gct)} / {fmt_ms(gir)}",
                        f"{alloc} / {o['peak_rss_kb'] / 1024:.1f}"]
            rows.append(row)
        header = ["input / pipeline"]
        for label in labels:
            header += [f"{label}: wall", f"{label}: gct / gir", f"{label}: allocs / RSS MB"]
        out.append(table(header, rows))
    return "\n".join(out)


# ---- commands -----------------------------------------------------------------------------

def default_home() -> Path:
    return Path(os.environ.get("CAMEL_HOME") or REPO / "out" / "latest")


def parse_versions(specs: Optional[List[str]]) -> List[Tuple[str, Path]]:
    if not specs:
        return [("current", default_home())]
    out = []
    for spec in specs:
        label, _, home = spec.partition("=")
        if not home:
            raise SystemExit(f"--camel wants LABEL=CAMEL_HOME, got '{spec}'")
        out.append((label, Path(home).resolve()))
    return out


def cmd_run(args) -> int:
    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    work = out_dir / "work"
    work.mkdir(exist_ok=True)
    sets = args.sets.split(",")
    counts = [int(x) for x in args.modules.split(",")]
    versions = parse_versions(args.camel)
    for label, home in versions:
        if not tool_path(home, "camel").exists():
            raise SystemExit(f"no camel under {home} ({label})")
    inputs = build_inputs(sets, work, counts, args.large_size)
    pipelines = args.pipelines.split(",")
    data = measure(inputs, versions, args.rounds, args.cpu, work, pipelines, build_shim(work))
    (out_dir / "results.json").write_text(json.dumps(data, indent=1), encoding="utf-8", newline="\n")
    text = render(data)
    (out_dir / "results.md").write_text(text, encoding="utf-8", newline="\n")
    print(text)
    failed = [r for r in data["results"] if "error" in r]
    for r in failed:
        print(f"bench: {r['input']} / {r['pipeline']} ({r['version']}): {r['error']}", file=sys.stderr)
    return 1 if failed else 0


def cmd_table(args) -> int:
    data = json.loads((Path(args.dir) / "results.json").read_text(encoding="utf-8"))
    print(render(data))
    return 0


def cmd_check(_args) -> int:
    """The smoke run: every part of the benchmark once on the smallest inputs, no timings asserted."""
    with tempfile.TemporaryDirectory(prefix="camel-bench-") as tmp:
        work = Path(tmp)
        inputs = build_inputs(["small", "modules"], work, [3], LARGE_SIZE)
        inputs = [i for i in inputs if i["id"] in ("arith10", "chain-3")]
        shim = build_shim(work)
        data = measure(inputs, [("current", default_home())], 1, None, work, ["compile", "passes", "fvm"], shim)
        expected_own = {item["id"]: item.get("own_modules") for item in inputs}
        problems = []
        for r in data["results"]:
            name = f"{r['input']}/{r['pipeline']}"
            if "error" in r:
                problems.append(f"{name}: {r['error']}")
                continue
            phases = r["phases_ms"]
            for kind in ("startup", "parse", "gct", "gir", "encode", "teardown", "pass:std::null"
                         if r["pipeline"] != "fvm" else "pass:std::fvm"):
                if kind not in phases:
                    problems.append(f"{name}: no phase '{kind}'")
            if r["pipeline"] == "passes" and "pass:std::opt::inline" not in phases:
                problems.append(f"{name}: the passes are not phases")
            # The module numbers. `counters.modules` counts every module compiled from Camel
            # source, libraries included; `modules_user` is the program's own modules (what the
            # input set varies) and `modules_library` the ones a pass or the program imported
            # (stdlib/rules.cml and its declaration module on the `passes` pipeline). Both are
            # cost: the user count is pinned to what the project has, and so is the library set.
            loaded = r.get("modules_loaded", [])
            user = r.get("modules_user", [])
            library = r.get("modules_library", [])
            counter = r.get("counters", {}).get("modules")
            if not loaded:
                problems.append(f"{name}: the phase report names no module: {r.get('counters')}")
            elif counter != len(loaded):
                problems.append(
                    f"{name}: the modules counter says {counter} but the phases name {len(loaded)}: "
                    f"{loaded} (the program's own: {user}; library: {library})")
            own = expected_own.get(r["input"])
            if own is None:
                own = [r["modules_entry"]] if r.get("modules_entry") else []
            if sorted(user) != sorted(own):
                problems.append(
                    f"{name}: the program's own modules are {own} ({len(own)}), but the run loaded "
                    f"{len(loaded)} in total: own {user}; library {library}")
            if r["pipeline"] == "passes":
                if sorted(library) != sorted(PASS_LIBRARY_MODULES):
                    problems.append(
                        f"{name}: loaded library modules {library}; expected "
                        f"{list(PASS_LIBRARY_MODULES)} (see PASS_LIBRARY_MODULES: the on-demand "
                        f"rules/graph/math import of std::opt). The program's own modules were {user}")
            elif library:
                problems.append(
                    f"{name}: the {r['pipeline']} pipeline loaded library modules {library}; "
                    f"expected none (only a pass compiles a library on demand). The program's own "
                    f"modules were {user}")
            if r.get("counters", {}).get("gct.nodes", 0) <= 0 or r.get("counters", {}).get("graph.nodes", 0) <= 0:
                problems.append(f"{name}: object counters missing: {r.get('counters')}")
            if shim is not None and r.get("alloc", {}).get("allocations", 0) <= 0:
                problems.append(f"{name}: no allocation counts under the shim")
        text = render(data)
        if "## Compile only" not in text or "## Modules" not in text:
            problems.append("the Markdown report misses a section")
        # The option as a user writes it: bare (the report goes to stderr) and with a file,
        # in front of a program that then runs as without it.
        camel = str(tool_path(default_home(), "camel"))
        plain = inputs[0]["path"]
        for form, report_file in (("--time-phases", None), ("--time-phases=", work / "by_flag.json")):
            argument = form + (str(report_file) if report_file else "")
            done = subprocess.run([camel, argument, plain], env=camel_env(default_home(), {}),
                                  capture_output=True, text=True, cwd=tmp)
            text = report_file.read_text(encoding="utf-8") if report_file and report_file.exists() else done.stderr
            try:
                phases = json.loads(text[text.index("{"):])["phases"]
            except (ValueError, KeyError):
                phases = []
            if done.returncode != 0 or done.stdout.strip() != "25" or not any(p["name"] == "gct" for p in phases):
                problems.append(f"`camel {argument} <program>`: exit {done.returncode}, "
                                f"output {done.stdout.strip()!r}, {len(phases)} phases")
        # The environment variable asks for the report where the option cannot be given.
        by_env = work / "by_env.json"
        subprocess.run([str(tool_path(default_home(), "camel")), inputs[0]["path"], "std::null"],
                       env=camel_env(default_home(), {"CAMEL_TIME_PHASES": str(by_env)}),
                       stdout=subprocess.DEVNULL, cwd=tmp)
        if not by_env.exists() or "phases" not in json.loads(by_env.read_text(encoding="utf-8")):
            problems.append("CAMEL_TIME_PHASES did not produce a report")
        if problems:
            print("FAIL\n" + "\n".join(problems))
            return 1
        print(f"ok compile benchmark: {len(data['results'])} cases, allocation shim "
              f"{'used' if shim is not None else 'not available'}")
        # Which modules, not just how many: the count alone is what stayed red for so long.
        for r in data["results"]:
            own, library = r.get("modules_user", []), r.get("modules_library", [])
            print(f"  {r['input']}/{r['pipeline']}: {len(r.get('modules_loaded', []))} modules loaded: "
                  f"{len(own)} user ({', '.join(own)}); {len(library)} library "
                  f"({', '.join(library) or 'none'})")
        return 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("run")
    p.add_argument("--out", default=str(HERE / "out"))
    p.add_argument("--rounds", type=int, default=9)
    p.add_argument("--cpu", type=int, default=2, help="CPU to pin every run to (taskset -c)")
    p.add_argument("--sets", default="small,medium,large,modules")
    p.add_argument("--pipelines", default=",".join(RUN_PIPELINES))
    p.add_argument("--modules", default=",".join(str(n) for n in DEFAULT_MODULE_COUNTS))
    p.add_argument("--large-size", type=int, default=LARGE_SIZE, help="progen --size of the large program")
    p.add_argument("--camel", action="append", help="LABEL=CAMEL_HOME; repeat to compare versions")
    p.set_defaults(fn=cmd_run)
    p = sub.add_parser("table")
    p.add_argument("dir")
    p.set_defaults(fn=cmd_table)
    p = sub.add_parser("check")
    p.set_defaults(fn=cmd_check)
    args = ap.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    sys.exit(main())
