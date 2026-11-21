#!/usr/bin/env python3
"""E9 differential testing: progen programs x pipelines x back ends x collectors, against model.py.

    e9_difftest.py run     --n 500 [--start 1]     run the matrix, write results/<pin>/e9_cells.jsonl
    e9_difftest.py analyze                          per program / per root cause, localization -> e9_summary.json
    e9_difftest.py reduce  [--max-groups K]         reduce one representative per root cause with reduce.py

Programs: test/tools/progen generate(seed) with the default features, size 1, seeds start..start+n-1. The expected
output is computed by the independent evaluator model.py (the "reference"). A program the compiler rejects on
the first cell (diagnostic of stage parse/typecheck) is a generator reject, listed and not part of the matrix.

Pipelines (pass lists before the back end; each pass is the one `std::opt` runs, in the order of one round of
src/passes/opt/opt/opt.cpp `simplifyGraph`: devirtualize/specialize/inline (call rewriting), fold, cse, algebra,
dce; `order` is run once at the end):
    p00_none                    no pass
    p01_macro                   std::macro
    p02..p09_+<stage>           std::macro + the first k stages of [devirtualize specialize inline fold cse algebra dce order]
                                (each as its own pass, std::opt::<stage>)
    p10_opt                     std::macro std::opt   (the integrated fixpoint)
Back ends: std::nvm, std::fvm, std::jit, std::prl (the parallel scheduler; `std::tf` is its alias, see run-tests) and
std::cpp (`camel f.cml <passes> std::cpp` > x.cpp, compiled by clang++ -std=c++20 -O2 -ffp-contract=off
-I$CAMEL_HOME/include, the command camel-cpp runs, then executed).
Collectors: `--gc generational` and `--gc mark-sweep` (docs/cli.md "The collector"), given after the file name. They
apply to the four interpreting back ends. A std::cpp program is standalone, has no Camel runtime and no collector:
one cell per pipeline, collector "-".

Cell outcomes are those of test/tools/progen/runner.py (OK, REFUSED, REFUSED_UNLOCATED, MISMATCH, RUN_ERROR,
ERROR_NO_DIAG, CRASH, HANG, FRONTEND, NOT_RUN) plus, for std::cpp, CPP_COMPILE_ERROR (clang++ failed on generated code).
REFUSED (a located diagnostic of a rewrite pass, nothing printed before it) is a legal way to decline and is not a failure.
"""
import argparse, collections, hashlib, json, os, re, shutil, subprocess, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))
from common import *

PG = SRC / "test/tools/progen"
sys.path.insert(0, str(PG))
import runner as R  # noqa: E402
from generate import generate  # noqa: E402
from model import emit, emit_modules  # noqa: E402
R.CAMEL = CAMEL

OPT_STAGES = ["devirtualize", "specialize", "inline", "fold", "cse", "algebra", "dce", "order"]
PIPES = [("p00_none", []), ("p01_macro", ["std::macro"])]
for k in range(1, len(OPT_STAGES) + 1):
    PIPES.append((f"p{k+1:02d}_+{OPT_STAGES[k-1]}", ["std::macro"] + [f"std::opt::{s}" for s in OPT_STAGES[:k]]))
PIPES.append(("p10_opt", ["std::macro", "std::opt"]))
BACKENDS = ["std::nvm", "std::fvm", "std::jit", "std::prl"]
COLLECTORS = ["generational", "mark-sweep"]
BAD = set(R.BAD) | {"CPP_COMPILE_ERROR"}
WORK = EVAL / "e9work"
CXXFLAGS = ["-std=c++20", "-O2", "-ffp-contract=off", f"-I{PINDIR}/include"]


def cell_ids():
    out = []
    for pid, _ in PIPES:
        for b in BACKENDS:
            for c in COLLECTORS:
                out.append((pid, b, c))
        out.append((pid, "std::cpp", "-"))
    return out


def lines_of(text):
    got = text.split("\n")
    if got and got[-1] == "":
        got.pop()
    return got


def run_cpp(path, passes, env_values, expected, cache):
    env = dict(ENV, **env_values)
    r = run([CAMEL, path.name, *passes, "std::cpp"], timeout=60, cwd=path.parent, env=env)
    if r["timeout"]:
        return dict(outcome="HANG", detail="std::cpp translation timed out")
    if r["rc"] != 0:
        txt = re.sub(r"\x1b\[[0-9;]*m", "", r["err"] + r["out"])
        m = re.search(r"error\[([A-Za-z:]+)\]", txt)
        located = "-->" in txt
        if r["rc"] < 0:
            return dict(outcome="CRASH", detail=f"std::cpp translation: signal {-r['rc']}")
        stage_pass = "std::cpp" in txt and ("pass:" in txt or "Translation" in txt or "cannot" in txt)
        o = "REFUSED" if (m and located) else "ERROR_NO_DIAG"
        return dict(outcome=o, diag=m.group(1) if m else "", detail=txt.strip()[:200])
    src = r["out"]
    h = hashlib.sha1(src.encode()).hexdigest()
    if h in cache:
        return dict(cache[h], cached=True)
    cpp = path.parent / f"x_{h[:10]}.cpp"
    exe = path.parent / f"x_{h[:10]}"
    cpp.write_text(src, encoding="utf-8")
    c = run(["clang++", *CXXFLAGS, cpp, "-o", exe], timeout=300, cwd=path.parent, env=env)
    if c["rc"] != 0:
        res = dict(outcome="CPP_COMPILE_ERROR", detail=(c["err"] or c["out"]).strip()[:300], cpp_sha1=h[:10])
    else:
        x = run([exe], timeout=60, cwd=path.parent, env=env)
        got = lines_of(x["out"])
        if x["timeout"]:
            res = dict(outcome="HANG", detail="executable timed out")
        elif x["rc"] is None or x["rc"] < 0:
            res = dict(outcome="CRASH", detail=f"signal {-x['rc']}")
        elif x["rc"] != 0:
            res = dict(outcome="RUN_ERROR", detail=f"exit {x['rc']}: {(x['err'] or x['out']).strip()[-200:]}", got=got[:60])
        elif got == expected:
            res = dict(outcome="OK")
        else:
            res = dict(outcome="MISMATCH", detail=R.first_difference(expected, got), got=got[:60])
    cache[h] = res
    for f in (cpp, exe):
        try: f.unlink()
        except OSError: pass
    return res


def one(seed):
    prog, expected, attempts = generate(seed)
    source = emit(prog)
    d = WORK / f"seed_{seed}"
    d.mkdir(parents=True, exist_ok=True)
    name = f"progen_{seed}"
    path = d / f"{name}.cml"
    path.write_text(source, encoding="utf-8", newline="\n")
    for stem, text in emit_modules(prog).items():
        (d / f"{stem}.cml").write_text(text, encoding="utf-8", newline="\n")
    env_values = prog.a["env"]
    rec = dict(seed=seed, lines=len(source.splitlines()), expected_lines=len(expected), cells={}, bad={}, other={})
    crashes, cache = 0, {}
    first = True
    for pid, b, c in cell_ids():
        cid = f"{pid}|{b}|{c}"
        pipe = dict(PIPES)[pid]
        if crashes >= 6:
            rec["cells"][cid] = "NOT_RUN"; continue
        if b == "std::cpp":
            info = run_cpp(path, pipe, env_values, expected, cache)
        else:
            info = R.run_cell(path, ["--gc", c, *pipe, b], env_values, expected, 30.0)
        if first:
            first = False
            if info["outcome"] == "FRONTEND":
                rec.update(rejected=info.get("diag", ""), reject_detail=info.get("detail", "")[:200])
                shutil.rmtree(d, ignore_errors=True)
                return rec
        rec["cells"][cid] = info["outcome"]
        if info["outcome"] != "OK":
            tgt = rec["bad"] if info["outcome"] in BAD else rec["other"]
            tgt[cid] = {k: (v[:240] if isinstance(v, str) else v) for k, v in info.items() if k in ("outcome", "diag", "detail", "stage", "located", "exit", "cpp_sha1")}
            if "got" in info: tgt[cid]["got"] = info["got"][:6]
        crashes += info["outcome"] in ("CRASH", "HANG")
    rec["expected"] = expected[:6]
    if not rec["bad"]:
        shutil.rmtree(d, ignore_errors=True)
    return rec


def cmd_run(a):
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    seeds = list(range(a.start, a.start + a.n))
    rows = pmap(one, seeds, progress=25)
    write_jsonl(OUT / "e9_cells.jsonl", rows)
    ok = sum(1 for r in rows if not r.get("rejected"))
    print(json.dumps(dict(programs=len(rows), rejected=sum(1 for r in rows if r.get("rejected")), in_matrix=ok,
                          failing=sum(1 for r in rows if r.get("bad"))), indent=1))


# ------------------------------------------------------------------------------------------ analysis
def column(rec, b, c):
    return {pid: rec["cells"].get(f"{pid}|{b}|{c}") for pid, _ in PIPES}


def stage_of(pid):
    fixed = {"p00_none": "(no pass: front end or back end)", "p01_macro": "std::macro", "p10_opt": "std::opt (integrated fixpoint)"}
    return fixed[pid] if pid in fixed else "std::opt::" + pid.split("+")[1]


def localize(rec):
    """First pipeline whose std::nvm (generational) result is bad; None if std::nvm is clean everywhere."""
    col = column(rec, "std::nvm", "generational")
    for pid, _ in PIPES:
        if col.get(pid) in BAD:
            return pid
    return None


def signature(rec):
    """Root cause key of a failing program."""
    loc = localize(rec)
    bad = rec["bad"]
    first_cid = next(iter(sorted(bad, key=lambda c: (c.split("|")[0], c))))
    if loc:
        pid = loc
        info = bad[f"{loc}|std::nvm|generational"]
        where = "pipeline:" + stage_of(pid)
        backends = sorted({c.split("|")[1] for c in bad})
    else:
        backends = sorted({c.split("|")[1] for c in bad})
        # nvm is clean in every prefix: the fault is in a back end or collector; name the first bad pipeline on the
        # first bad back end
        b0 = backends[0]
        pid = min((c.split("|")[0] for c in bad if c.split("|")[1] == b0))
        info = next(v for c, v in sorted(bad.items()) if c.startswith(pid + "|" + b0))
        where = "backend:" + ",".join(backends) + " first bad at " + stage_of(pid)
    colls = sorted({c.split("|")[2] for c in bad})
    return dict(where=where, outcome=info["outcome"], diag=info.get("diag", ""), backends=backends, collectors=colls, first_pipeline=pid)


def cmd_analyze(a):
    rows = [json.loads(l) for l in open(OUT / "e9_cells.jsonl", encoding="utf-8")]
    rej = [r for r in rows if r.get("rejected")]
    run_rows = [r for r in rows if not r.get("rejected")]
    failing = [r for r in run_rows if r.get("bad")]
    cell_counts = collections.Counter(o for r in run_rows for o in r["cells"].values())
    per_cell = collections.defaultdict(collections.Counter)
    for r in run_rows:
        for cid, o in r["cells"].items():
            pid, b, c = cid.split("|")
            per_cell[f"{b}/{c}"][o] += 1
    groups = collections.defaultdict(list)
    for r in failing:
        s = signature(r)
        r["signature"] = s
        key = f"{s['where']} | {s['outcome']}{(':' + s['diag']) if s['diag'] else ''}"
        groups[key].append(r["seed"])
    # per pipeline failure counts (programs with a bad cell in the pipeline)
    per_pipe = {pid: sum(1 for r in failing if any(c.startswith(pid + "|") for c in r["bad"])) for pid, _ in PIPES}
    per_backend = {}
    for b in BACKENDS + ["std::cpp"]:
        per_backend[b] = sum(1 for r in failing if any(c.split("|")[1] == b for c in r["bad"]))
    per_collector = {c: sum(1 for r in failing if any(k.split("|")[2] == c for k in r["bad"])) for c in COLLECTORS}
    refused = collections.Counter()
    for r in run_rows:
        for cid, o in r["cells"].items():
            if o in ("REFUSED",):
                refused[cid.split("|")[0]] += 1
    summ = dict(
        pin=PIN, programs=len(rows), generator_rejects=len(rej),
        reject_reasons=dict(collections.Counter(r["rejected"] for r in rej)),
        programs_in_matrix=len(run_rows), cells_per_program=len(cell_ids()), cells_total=sum(len(r["cells"]) for r in run_rows),
        cell_outcomes=dict(cell_counts), programs_failing=len(failing),
        programs_failing_share=round(len(failing) / len(run_rows), 4) if run_rows else None,
        failing_programs_per_pipeline=per_pipe, failing_programs_per_backend=per_backend,
        failing_programs_per_collector=per_collector,
        legal_refusals_cells_per_pipeline=dict(refused),
        cell_outcomes_by_column={k: dict(v) for k, v in per_cell.items()},
        root_causes=[dict(key=k, programs=len(v), seeds=sorted(v)[:40]) for k, v in sorted(groups.items(), key=lambda kv: -len(kv[1]))],
    )
    write_json(OUT / "e9_summary.json", summ)
    write_jsonl(OUT / "e9_failing_programs.jsonl", [dict(seed=r["seed"], signature=r["signature"], bad=r["bad"], expected=r.get("expected")) for r in failing])
    print(json.dumps({k: v for k, v in summ.items() if k not in ("cell_outcomes_by_column",)}, indent=1)[:6000])


# ------------------------------------------------------------------------------------------ reduction
def cmd_reduce(a):
    from reduce import Reducer
    from generate import generate as gen
    rows = [json.loads(l) for l in open(OUT / "e9_failing_programs.jsonl", encoding="utf-8")]
    groups = collections.defaultdict(list)
    for r in rows:
        s = r["signature"]
        groups[(s["where"], s["outcome"], s["diag"])].append(r)
    out = []
    outdir = OUT / "e9_reduced"
    outdir.mkdir(parents=True, exist_ok=True)
    for gi, (key, members) in enumerate(sorted(groups.items(), key=lambda kv: -len(kv[1]))[:a.max_groups]):
        # representative: the shortest source
        members.sort(key=lambda r: r["seed"])
        rep = None
        for m in members[:8]:
            rep = m
            break
        s = rep["signature"]
        # the failing cell: the first bad pipeline on the localizing column
        bad = rep["bad"]
        if s["where"].startswith("pipeline:"):
            cid = f"{s['first_pipeline']}|std::nvm|generational"
        else:
            cid = next(c for c in sorted(bad) if c.startswith(s["first_pipeline"] + "|" + s["backends"][0]))
        pid, b, c = cid.split("|")
        rec = dict(group=" | ".join(key), programs=len(members), seed=rep["seed"], cell=cid)
        if b == "std::cpp":
            rec["reduced"] = "not attempted: reduce.py runs the camel back ends only"
            out.append(rec); continue
        prog, expected, _ = gen(rep["seed"])
        passes = ["--gc", c, *dict(PIPES)[pid], b]
        res = R.run_program(emit(prog), prog.a["env"], expected, {"fail": passes, "ref": []}, None, f"progen_{rep['seed']}", modules=emit_modules(prog))
        target = R.signature(res["fail"])
        rec["target"] = target
        rec["ref_outcome"] = res["ref"]["outcome"]
        if res["fail"]["outcome"] not in BAD:
            rec["reduced"] = f"did not reproduce on re-run ({res['fail']['outcome']})"
            out.append(rec); continue
        red = Reducer(prog, passes, target, reference_ok=res["ref"]["outcome"] == "OK", max_tests=3000)
        small, small_passes = red.run()
        name = f"rc{gi:02d}_seed{rep['seed']}"
        small.a["module"] = name
        exp2 = __import__("model").evaluate(small)
        src = emit(small)
        (outdir / f"{name}.cml").write_text(src, encoding="utf-8", newline="\n")
        # reproduce the written file 5 times
        seen = collections.Counter()
        shown = None
        for _ in range(5):
            rr = R.run_program(src, small.a["env"], exp2, {"fail": small_passes}, outdir, name, modules=emit_modules(small))["fail"]
            seen[rr["outcome"]] += 1
            if rr["outcome"] in BAD and shown is None: shown = rr
        rec.update(reduced=name + ".cml", reduced_lines=len(src.splitlines()), passes=small_passes, expected=exp2[:20],
                   observed=(shown or {}).get("got"), observed_outcome=(shown or {}).get("outcome"), detail=(shown or {}).get("detail"),
                   diag=(shown or {}).get("diag"), reproduced_of_5=dict(seen), tests=red.tests)
        out.append(rec)
        print(json.dumps(rec)[:400])
    write_json(OUT / "e9_reduced.json", out)


def main():
    ap = argparse.ArgumentParser()
    sp = ap.add_subparsers(dest="cmd", required=True)
    p = sp.add_parser("run"); p.add_argument("--n", type=int, default=500); p.add_argument("--start", type=int, default=1)
    sp.add_parser("analyze")
    p = sp.add_parser("reduce"); p.add_argument("--max-groups", type=int, default=12)
    a = ap.parse_args()
    {"run": cmd_run, "analyze": cmd_analyze, "reduce": cmd_reduce}[a.cmd](a)


main()
