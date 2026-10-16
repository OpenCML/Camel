#!/usr/bin/env python3
"""Synthetic checks of classify.py: under the frozen rules, payload comparison by value and its
hash fallback, tolerances, the benchmark timing mask, the time limit, written files; under
`frozen+D1-D9`, the deviations D2 (unlocated refusals), D4 (exclusion list), D5 (must-refuse
pruning) and D7 (file-writing operators compared through written content); under the newest
rule set, D10 (arrays element by element, sums relative to the sum of absolute values) and R1
(defect classes, LIBRARY_PRECONDITION, REQUIRES_STD_MACRO, refusal subjects, per-pass table).

    python3 benchmarks/pipeline/selftest.py      (exit 1 when a check fails)
"""
import copy
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import classify as c

R = c.RULESETS["frozen"]


def rec(effects, output="x\n", exit_code=0, mask=""):
    return {"effects": effects, "output": output, "output_sha1": "", "exit_code": exit_code, "timed_out": False,
            "wall_ms": 10.0, "diagnostics": None, "passes": ["std::nvm"], "pipeline_id": "P1", "output_mask": mask}


def ev(uri, payload=None, sha="h", truncated=False):
    e = {"uri": uri, "stage": "run", "origin": None, "payload_sha1": sha}
    if payload is not None:
        e["payload"] = payload
    if truncated:
        e["payload_truncated"] = True
    return e


def outcome(ref, got):
    return c.classify_native(ref, got, R, 10.0)["outcome"]


checks = []


def check(name, got, want):
    checks.append((name, got, want, got == want))


a = rec([ev(":io/println", "string=hello|||", "1")])
check("identical text", outcome(a, copy.deepcopy(a)), "CONSISTENT")
check("string differs", outcome(a, rec([ev(":io/println", "string=hellp|||", "2")])), "EFFECT_MISMATCH")
f = rec([ev(":op/assn", "float=0.5|float=1.0000001|||", "1")])
check("float within 1e-5", outcome(f, rec([ev(":op/assn", "float=0.5|float=1.0000002|||", "2")])), "CONSISTENT")
check("float beyond 1e-5", outcome(f, rec([ev(":op/assn", "float=0.5|float=1.001|||", "2")])), "EFFECT_MISMATCH")
i = rec([ev(":op/assn", "int=3|||", "1")])
check("int differs", outcome(i, rec([ev(":op/assn", "int=4|||", "2")])), "EFFECT_MISMATCH")
g = rec([ev(":op/assn", "float=100000|||", "1")])
check("float printed like an int, within tol", outcome(g, rec([ev(":op/assn", "float=100000.5|||", "2")])), "CONSISTENT")
check("float printed like ints both sides, within tol", outcome(g, rec([ev(":op/assn", "float=100001|||", "2")])), "CONSISTENT")
check("int typed, off by one", outcome(rec([ev(":op/assn", "int=100000|||", "1")]),
                                        rec([ev(":op/assn", "int=100001|||", "2")])), "EFFECT_MISMATCH")
n = rec([ev(":op/assn", "float=nan|||", "1")])
check("nan equals nan", outcome(n, copy.deepcopy(n)), "CONSISTENT")
h = rec([ev(":op/assn", sha="1")])
check("hash only, equal", outcome(h, rec([ev(":op/assn", sha="1")])), "CONSISTENT")
check("hash only, differ", outcome(h, rec([ev(":op/assn", sha="2")])), "UNDECIDED")
check("text one side only, hashes differ", outcome(f, rec([ev(":op/assn", sha="2")])), "UNDECIDED")
t = rec([ev(":op/assn", "Tensor=[1.0, 2.0, 3.0", "1", truncated=True)])
check("truncated, equal hash", outcome(t, copy.deepcopy(t)), "CONSISTENT")
check("truncated, prefix differs by value",
      outcome(t, rec([ev(":op/assn", "Tensor=[1.0, 9.0, 3.0", "2", truncated=True)])), "EFFECT_MISMATCH")
check("truncated, prefix equal, hashes differ",
      outcome(t, rec([ev(":op/assn", "Tensor=[1.0, 2.0, 3.0", "2", truncated=True)])), "UNDECIDED")
check("sequence differs", outcome(a, rec([ev(":io/print", "string=hello|||", "1")])), "EFFECT_MISMATCH")
check("macro operator ignored", outcome(a, rec([ev("autodiff:grad", "x", "9"), ev(":io/println", "string=hello|||", "1")])),
      "CONSISTENT")
m1 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=1.25 loss=0.5|||", "1")],
         output="CAMEL_BENCH latency_ms=1.25 loss=0.5\n", mask="camel_bench_ms")
m2 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=7.5 loss=0.5|||", "2")],
         output="CAMEL_BENCH latency_ms=7.5 loss=0.5\n", mask="camel_bench_ms")
m3 = rec([ev(":io/println", "string=CAMEL_BENCH latency_ms=7.5 loss=0.6|||", "3")],
         output="CAMEL_BENCH latency_ms=7.5 loss=0.6\n", mask="camel_bench_ms")
check("masked timing", outcome(m1, m2), "CONSISTENT")
check("masked timing, loss differs", outcome(m1, m3), "VALUE_MISMATCH")
check("unmasked timing", outcome(rec(m1["effects"], m1["output"]), rec(m2["effects"], m2["output"])), "VALUE_MISMATCH")
check("output near", c.classify_native(rec([], "v=1.0\n"), rec([], "v=1.0005\n"), R, 10.0)["near"], True)
check("output int differs", outcome(rec([], "n=3\n"), rec([], "n=4\n")), "VALUE_MISMATCH")
late = rec([]); late["wall_ms"] = 20000.0
check("late finish", outcome(rec([]), late), "RESOURCE_LIMIT")
fa = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "a", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.0]}}])
fb = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "b", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.000001]}}])
fc = dict(rec([]), files=[{"name": "o.npy", "bytes": 1, "sha1": "c", "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": [1.0, 2.5]}}])
check("npy within tol", outcome(fa, fb), "CONSISTENT")
check("npy beyond tol", outcome(fa, fc), "VALUE_MISMATCH")
check("file missing", outcome(fa, dict(rec([]), files=[])), "EFFECT_MISMATCH")

# ---- Deviations D2, D4, D5, D7 (CLASSIFIER-RULES.md section 12) --------------------------------
N = c.resolve_rules("frozen+D1-D9")       # the rule set these checks were written for
NEW = c.resolve_rules(c.NEWEST)
D1 = c.resolve_rules("frozen+D1")


def outcome_under(rules, ref, got):
    c.ACTIVE, c.STRIP_PAYLOAD_TYPES = rules, rules.payload_strip_types
    info = c.classify_cell(ref, [got], rules, lambda r: 10.0)
    c.ACTIVE, c.STRIP_PAYLOAD_TYPES = None, False
    return info


def npy(sha, values):
    return {"name": "o.npy", "bytes": 1, "sha1": sha, "npy": {"dtype": "<f4", "shape": [2], "count": 2, "values": values}}


def save(prefix, sha, files):
    return dict(rec([ev("tensor:save_npy", prefix, sha, truncated=True)]), files=files)


check("rule set name frozen+D1+D7", c.resolve_rules("frozen+D1+D7").file_ops_by_content, True)
check("rule set name frozen+D1+D7 has no D2", c.resolve_rules("frozen+D1+D7").unlocated_refusal_violation, False)
check("frozen+D1-D9 has every flag", (N.unlocated_refusal_violation, N.exclusion_list, N.must_refuse_static_only,
                                      N.file_ops_by_content, N.report_v2, N.npy_full_comparison, N.r1),
      (True,) * 5 + (False, False))
check("frozen+D1-D9 names D1..D9", c.deviations_of(N), [f"D{k}" for k in range(1, 10)])
check("newest rule set", (c.NEWEST, c.DEFAULT_RULES, NEW.npy_full_comparison, NEW.r1, NEW.report_v2,
                           NEW.read_order_free),
      ("frozen+D1-D11+R1", "frozen+D1-D11+R1", True, True, True, True))
check("newest names D1..D11 and R1", c.deviations_of(NEW), [f"D{k}" for k in range(1, 12)] + ["R1"])

# D11: the order of pure reads is not observable behaviour.
OLD = c.resolve_rules("frozen+D1-D10+R1")
check("the rule set before D11 still resolves, without the flag", (OLD.read_order_free, OLD.r1), (False, True))


def env(name):
    return ev("sys:get_env", f"string={name}|||", name)


def load(path, truncated=False):
    return ev("tensor:load_npy", f"string={path}|||", path, truncated)


def wr(path):
    return ev("fs:write_text", f"string={path}|string=1 2 3|||", "w" + path)


pr = ev(":io/println", "string=hello|||", "p1")
pr2 = ev(":io/println", "string=world|||", "p2")
e_ab, e_ba = rec([env("A"), env("B"), pr]), rec([env("B"), pr, env("A")])
check("D11 off: two environment reads in the other order", outcome_under(OLD, e_ab, e_ba)["outcome"], "EFFECT_MISMATCH")
check("D11: two environment reads in the other order, one after a print", outcome_under(NEW, e_ab, e_ba)["outcome"],
      "CONSISTENT")
check("D11: another variable is read", outcome_under(NEW, e_ab, rec([env("A"), env("C"), pr]))["outcome"],
      "EFFECT_MISMATCH")
check("D11: a read is missing", outcome_under(NEW, e_ab, rec([env("A"), pr]))["outcome"], "EFFECT_MISMATCH")
check("D11: a read happens twice", outcome_under(NEW, e_ab, rec([env("A"), env("B"), env("B"), pr]))["outcome"],
      "EFFECT_MISMATCH")
check("D11: prints keep their order", outcome_under(NEW, rec([env("A"), pr, pr2]), rec([env("A"), pr2, pr]))["outcome"],
      "EFFECT_MISMATCH")
l_ab, l_ba = rec([load("w/a.npy"), load("w/b.npy"), pr]), rec([load("w/b.npy"), load("w/a.npy"), pr])
check("D11 off: two file reads in the other order", outcome_under(OLD, l_ab, l_ba)["outcome"], "EFFECT_MISMATCH")
check("D11: two file reads in the other order", outcome_under(NEW, l_ab, l_ba)["outcome"], "CONSISTENT")
check("D11: file and environment reads interleaved differently",
      outcome_under(NEW, rec([env("A"), load("w/a.npy"), env("B"), load("w/b.npy")]),
                    rec([load("w/b.npy"), env("B"), load("w/a.npy"), env("A")]))["outcome"], "CONSISTENT")
check("D11: another file is read", outcome_under(NEW, l_ab, rec([load("w/a.npy"), load("w/c.npy"), pr]))["outcome"],
      "EFFECT_MISMATCH")
w_then_r, r_then_w = rec([wr("o/x.npy"), load("o/x.npy")]), rec([load("o/x.npy"), wr("o/x.npy")])
check("D11: a file read moved across a file write stays a mismatch", outcome_under(NEW, w_then_r, r_then_w)["outcome"],
      "EFFECT_MISMATCH")
check("D11: file reads reordered on one side of a file write",
      outcome_under(NEW, rec([load("w/a.npy"), load("w/b.npy"), wr("o/x.npy"), load("w/c.npy")]),
                    rec([load("w/b.npy"), load("w/a.npy"), wr("o/x.npy"), load("w/c.npy")]))["outcome"], "CONSISTENT")
check("D11: an environment read may move across a file write",
      outcome_under(NEW, rec([env("A"), wr("o/x.npy")]), rec([wr("o/x.npy"), env("A")]))["outcome"], "CONSISTENT")
check("D11: a read with a truncated payload keeps its place",
      outcome_under(NEW, rec([load("w/a.npy", True), pr]), rec([pr, load("w/a.npy", True)]))["outcome"], "EFFECT_MISMATCH")
check("D11: other effects are not reads (random numbers)",
      outcome_under(NEW, rec([ev("rand:rand", "||", "r"), pr]), rec([pr, ev("rand:rand", "||", "r")]))["outcome"],
      "EFFECT_MISMATCH")
check("D11: a trace without reads is returned as it is", c.order_free_reads([pr, pr2]), [pr, pr2])
s0 = save("Tensor=tensor([1.0, 2.0", "p0", [npy("a", [1.0, 2.0])])
s1 = save("Tensor=tensor([1.0, 2.0", "p1", [npy("b", [1.0, 2.000001])])
s2 = save("Tensor=tensor([1.0, 2.1", "p2", [npy("b", [1.0, 2.000001])])
s3 = save("Tensor=tensor([1.0, 2.0", "p3", [npy("c", [1.0, 2.5])])
check("D7 off: truncated file op, hashes differ", outcome_under(D1, s0, s1)["outcome"], "UNDECIDED")
check("D7: truncated file op, written content equal", outcome_under(N, s0, s1)["outcome"], "CONSISTENT")
check("D7: mode recorded", outcome_under(N, s0, s1)["payload_mode"], "files")
check("D7 off: printed prefix differs (4 digits)", outcome_under(D1, s0, s2)["outcome"], "EFFECT_MISMATCH")
check("D7: printed prefix differs, written content equal", outcome_under(N, s0, s2)["outcome"], "CONSISTENT")
check("D7: written content differs", outcome_under(N, s0, s3)["outcome"], "VALUE_MISMATCH")
check("D7: nothing written to compare", outcome_under(N, save("x", "p0", []), save("x", "p1", []))["outcome"], "UNDECIDED")
check("D7: not a file operator, still by hash",
      outcome_under(N, rec([ev(":op/assn", "Tensor=[1.0", "1", truncated=True)]),
                    rec([ev(":op/assn", "Tensor=[1.0", "2", truncated=True)]))["outcome"], "UNDECIDED")
m0 = dict(rec([ev("onnx:export_model", "f|x", "p0", truncated=True)]), files=[{"name": "m.onnx", "bytes": 9, "sha1": "a"}])


def model(sha, vs_ref=None):
    entry = {"name": "m.onnx", "bytes": 9, "sha1": sha}
    if vs_ref is not None:
        entry["vs_ref"] = vs_ref
    return dict(rec([ev("onnx:export_model", "f|x", "p1", truncated=True)]), files=[entry])


check("D7: model identical bytes", outcome_under(N, m0, model("a"))["outcome"], "CONSISTENT")
check("D7: model differs by hash, no content comparison", outcome_under(N, m0, model("b"))["outcome"], "UNDECIDED")
check("D7: model differs by hash, equivalent on ONNX Runtime",
      outcome_under(N, m0, model("b", {"equivalent": True, "within_near": True}))["outcome"], "CONSISTENT")
check("D7: model differs on ONNX Runtime",
      outcome_under(N, m0, model("b", {"equivalent": False, "within_near": False, "detail": "max |diff| 0.5"}))["outcome"],
      "VALUE_MISMATCH")


def big(sha, sample, vs_ref=None):
    d = {"dtype": "<f4", "shape": [9000], "count": 9000, "sample": sample, "sum": sum(sample), "abs_sum": sum(sample)}
    if vs_ref is not None:
        d["vs_ref"] = vs_ref
    return dict(rec([ev("tensor:save_npy", "Tensor=tensor([1.0", sha, truncated=True)]),
                files=[{"name": "o.npy", "bytes": 1, "sha1": sha, "npy": d}])


b0 = big("a", [1.0, 2.0])
P10 = c.resolve_rules("frozen+D1-D9+D10")
differs = {"within_strict": False, "within_near": False, "max_scaled_err": 0.5, "elements": 9000}
within = {"within_strict": True, "within_near": True, "max_scaled_err": 1e-7, "elements": 9000}
check("rule set frozen+D1-D9+D10", (P10.npy_full_comparison, P10.file_ops_by_content, N.npy_full_comparison),
      (True, True, False))
check("large array, sample equal: judged on the sample", outcome_under(N, b0, big("b", [1.0, 2.0]))["file_sampled"], True)
check("default rules: the full comparison is recorded but not used",
      outcome_under(N, b0, big("b", [1.0, 2.0], differs))["outcome"], "CONSISTENT")
check("default rules: sums differ although every element agrees",
      outcome_under(N, b0, big("b", [1.0, 2.0001], within))["outcome"], "VALUE_MISMATCH")
check("D10: full comparison differs", outcome_under(P10, b0, big("b", [1.0, 2.0], differs))["outcome"],
      "VALUE_MISMATCH")
check("D10: full comparison within tolerance",
      (lambda i: (i["outcome"], i["file_sampled"], i["file_compare_mode"]))(
          outcome_under(P10, b0, big("b", [1.0, 2.0001], within))),
      ("CONSISTENT", False, "all_elements_vs_ref"))


def big2(sha, sample, total, abs_total):
    d = {"dtype": "<f4", "shape": [9000], "count": 9000, "sample": sample, "sum": total, "abs_sum": abs_total}
    return dict(rec([ev("tensor:save_npy", "Tensor=tensor([1.0", sha, truncated=True)]),
                files=[{"name": "o.npy", "bytes": 1, "sha1": sha, "npy": d}])


# A record without all elements: the sample element by element, the sums relative to abs_sum.
g0 = big2("a", [1.0, -2.0], -3.707, 27730.8)
i_sum = outcome_under(NEW, g0, big2("b", [1.0000001, -2.0], -3.707084, 27730.8))
check("frozen+D1-D9: sum differs by 8.4e-5 at element tolerance",
      outcome_under(N, g0, big2("b", [1.0000001, -2.0], -3.707084, 27730.8))["outcome"], "VALUE_MISMATCH")
check("D10: the same sum difference relative to abs_sum (3e-9)",
      (i_sum["outcome"], i_sum["file_sampled"], i_sum["file_compare_mode"]),
      ("CONSISTENT", True, "sample_and_relative_sums"))
check("D10: a sampled element beyond 1e-5 is a mismatch",
      (lambda i: (i["outcome"], i["near"]))(outcome_under(NEW, g0, big2("b", [1.0001, -2.0], -3.707, 27730.8))),
      ("VALUE_MISMATCH", True))
check("D10: a sampled element beyond 1e-3",
      (lambda i: (i["outcome"], i["near"]))(outcome_under(NEW, g0, big2("b", [1.5, -2.0], -3.707, 27730.8))),
      ("VALUE_MISMATCH", False))
check("D10: sums beyond 1e-5 of abs_sum are a mismatch",
      outcome_under(NEW, g0, big2("b", [1.0, -2.0], -3.107, 27730.8))["outcome"], "VALUE_MISMATCH")
check("D10: byte-identical file is mode identical",
      outcome_under(NEW, g0, big2("a", [1.0, -2.0], -3.707, 27730.8))["file_compare_mode"], "identical")
check("D10: small array, all elements", outcome_under(NEW, s0, s1)["file_compare_mode"], "all_elements")
t0 = dict(rec([ev("fs:write_text", sha="p0")]), files=[{"name": "a.txt", "bytes": 5, "sha1": "a", "text": "v=1.0\n"}])
t1 = dict(rec([ev("fs:write_text", sha="p1")]), files=[{"name": "a.txt", "bytes": 9, "sha1": "b", "text": "v=1.0000001\n"}])
t2 = dict(rec([ev("fs:write_text", sha="p1")]), files=[{"name": "a.txt", "bytes": 5, "sha1": "c", "text": "v=2.0\n"}])
check("D7: text file within tolerance", outcome_under(N, t0, t1)["outcome"], "CONSISTENT")
check("D7: text file differs", outcome_under(N, t0, t2)["outcome"], "VALUE_MISMATCH")


def refusal(line):
    r = rec([], output="", exit_code=1)
    r["passes"], r["pipeline_id"] = ["std::macro", "onnx::export"], "E1"
    r["diagnostics"] = [{"severity": "Error", "message": "refused", "range": {"start": {"line": line, "character": 0},
                         "end": {"line": line, "character": 4 if line else 0}},
                         "data": {"stage": "pass:onnx::export", "name": "RuntimeDiag::TranslationRefused",
                                  "modulePath": "x.cml" if line else ""}}]
    return r


ok0 = rec([], output="")
for rules, tag in ((D1, "frozen+D1"), (N, "D2")):
    for line, name in ((3, "located"), (0, "unlocated")):
        info = outcome_under(rules, ok0, refusal(line))
        want = "REFUSED" if rules is D1 else ("REFUSED_LOCATED" if line else "REFUSED_UNLOCATED")
        check(f"{tag}: {name} refusal", info["outcome"], want)
        check(f"{tag}: {name} refusal is a violation", c.is_violation(info), rules is N and not line)
c.ACTIVE = N
r_un = refusal(0)
r_un.update(role="must_refuse", program="x.cml", program_id="x@refuse1", repeat=0)
check("D2: must-refuse, refused without a location", c.classify_refusal(r_un)["outcome"], "REFUSED_UNLOCATED")
check("D2: must-refuse, refused with a location", c.classify_refusal(dict(refusal(3), role="must_refuse"))["outcome"],
      "REFUSED_LOCATED")
c.ACTIVE = None
check("frozen: must-refuse refusal", c.classify_refusal(r_un)["outcome"], "REFUSED_AS_EXPECTED")
check("D5: new record, run-time expectation", c.removed_kind({"expect_kind": "run_time", "program_id": "p"}, None), "run_time")
check("D5: new record, front-end expectation", c.removed_kind({"expect_kind": "front_end", "program_id": "p"}, None), "")
check("D5: old record, looked up in the corpus", c.removed_kind({"program_id": "p"}, {"p": "not_stated"}), "not_stated")
check("D5: old record, no corpus", c.removed_kind({"program_id": "p"}, None), "")


def cell(prog, pid, repeat, output):
    r = rec([], output=output)
    r.update(program=prog, program_id=prog.replace("/", "_"), pipeline_id=pid, passes=[] if pid == "P0" else ["std::nvm"],
             repeat=repeat, corpus="dev", origin="test", role="agree")
    return r


recs = {}
for prog, outs in (("a/listed.cml", ["t=1\n"] * 3), ("a/unstable.cml", ["t=1\n", "t=2\n", "t=1\n"]), ("a/fine.cml", ["x\n"] * 3)):
    for pid in ("P0", "P1"):
        recs[(prog.replace("/", "_"), pid)] = [cell(prog, pid, k, outs[k]) for k in range(3)]
listed = {"excluded": {"a/listed.cml": {"path": "a/listed.cml", "reason": "prints a time"}}, "reviewed": {}}
rows, ref_rows, _, _, _ = c.classify_all(recs, N, 20.0, 10.0, listed, None)
by = {r["program"]: r["reference"] for r in ref_rows}
check("D4: listed program is excluded although its repeats agree", by["a/listed.cml"], "EXCLUDED")
check("D4: determinism check still catches an unlisted program", by["a/unstable.cml"], "NONDETERMINISTIC")
check("D4: other programs are judged", (by["a/fine.cml"], [r["program"] for r in rows]), ("REFERENCE", ["a/fine.cml"]))
rows, ref_rows, _, _, _ = c.classify_all(recs, D1, 20.0, 10.0, listed, None)
check("D4 off: the list is not used", {r["program"]: r["reference"] for r in ref_rows}["a/listed.cml"], "REFERENCE")
check("D9: signature without a diagnostic", c.signature_of({"diag_name": "", "detail": "near (within 1e-3); file params/b1_w.npy: max |diff| 8.4e-05 (within 1e-3)"}),
      "file <*.npy>: max |diff| N (within N)")
c.ACTIVE, c.STRIP_PAYLOAD_TYPES = None, False

# ---- R1 (CLASSIFIER-RULES.md section 13) ---------------------------------------------------------


def failing(message, stage="run", line=61, output="started\n", effects=()):
    r = rec(list(effects), output=output, exit_code=1)
    r["diagnostics"] = [{"severity": "Error", "message": message,
                         "range": {"start": {"line": line, "character": 1}, "end": {"line": line, "character": 5 if line else 1}},
                         "data": {"stage": stage, "name": "RuntimeDiag::RuntimeError", "modulePath": "m.cml" if line else ""}}]
    return r


AD = "An error has occurred: autodiff: branch arms must be subgraph calls; differentiate before inlining them."
lib = outcome_under(NEW, rec([]), failing(AD))
check("R1: autodiff run-time diagnostic is a library precondition, late",
      (lib["outcome"], lib["underlying_outcome"], lib["library"], lib["library_precondition"]),
      ("LIBRARY_PRECONDITION", "RUN_FAIL", "autodiff", "late_or_obscure"))
check("R1 off: the same cell is RUN_FAIL", outcome_under(N, rec([]), failing(AD))["outcome"], "RUN_FAIL")
check("R1: a library precondition is not a defect of a standard pass",
      (c.is_violation(lib), c.DEFECT_CLASS[lib["outcome"]]), (False, "library_precondition"))
other = outcome_under(NEW, rec([]), failing("An error has occurred: Array index out of bounds.."))
check("R1: another run-time error stays a defect",
      (other["outcome"], c.DEFECT_CLASS[other["outcome"]]), ("RUN_FAIL", "crash_hang_or_error"))
early = outcome_under(NEW, rec([], output=""), failing("autodiff: needs std::macro", stage="pass:std::opt", output=""))
check("R1: located library diagnostic of a pass, before execution",
      (early["outcome"], early["library_precondition"]), ("LIBRARY_PRECONDITION", "located_before_execution"))
obscure = outcome_under(NEW, rec([], output=""), failing("autodiff: needs std::macro", stage="pass:std::opt",
                                                         output="", line=0))
check("R1: the same without a location is obscure", obscure["library_precondition"], "late_or_obscure")
check("R1: prefix must start the message", c.library_of("An error has occurred: x in autodiff: y"), None)
check("R1: std::macro position", [c.macro_position(p) for p in (["std::nvm"], ["std::macro", "std::nvm"],
                                                                 ["std::opt::inline", "std::macro", "std::nvm"])],
      ["no std::macro in the pipeline", "std::macro first", "std::macro after std::opt::inline"])
check("R1: classes of the defect outcomes",
      [c.DEFECT_CLASS[o] for o in ("VALUE_MISMATCH", "EFFECT_MISMATCH", "CRASH", "HANG", "REFUSED_UNLOCATED", "CONSISTENT")],
      ["silent_result_change"] * 2 + ["crash_hang_or_error"] * 2 + ["diagnostic_unlocated_or_late", "agrees"])
check("R1 needs D2 and D9", (lambda: [c.resolve_rules("frozen+D1+D2+D9+R1").r1])(), [True])
try:
    c.resolve_rules("frozen+D1+R1")
    check("R1 without D2/D9 is rejected", "accepted", "rejected")
except SystemExit:
    check("R1 without D2/D9 is rejected", "rejected", "rejected")


def cell2(prog, pid, repeat, base):
    r = copy.deepcopy(base)
    r.update(program=prog, program_id=prog.replace("/", "_"), pipeline_id=pid, passes=c.pl.all_pipelines()[pid],
             repeat=repeat, corpus="dev", origin="test", role="agree")
    return r


AD0 = "An error has occurred: autodiff: cannot differentiate through a call of a function value."
recs = {}
for prog, by_pid in (
        ("a/needs_macro.cml", {"P0": failing(AD0, output=""), "P1": failing(AD0, output=""), "P4": rec([])}),
        ("a/broken.cml", {"P0": failing("An error has occurred: boom", output=""), "P4": rec([])}),
        ("a/never.cml", {"P0": failing(AD0, output=""), "P4": failing(AD0, output="")}),
        ("a/grad.cml", {"P0": rec([]), "P4": rec([]), "P11": failing(AD), "P1": failing("An error has occurred: boom")})):
    for pid, base in by_pid.items():
        recs[(prog.replace("/", "_"), pid)] = [cell2(prog, pid, k, base) for k in range(3)]
unknown = failing("Unrecognized graph rewriting/scheduling/translation pass 'x1a::no_such_pass'.",
                  stage="pass:x1a::no_such_pass", output="", line=0)
unknown["diagnostics"][0]["data"]["name"] = "RuntimeDiag::UnrecognizedGraphPass"
recs[("a_grad.cml", "XU1")] = [cell2("a/grad.cml", "XU1", k, unknown) for k in range(3)]
none = {"excluded": {}, "reviewed": {}}
rows, ref_rows, refusal_rows, _, _ = c.classify_all(recs, NEW, 20.0, 10.0, none, None)
by = {r["program"]: r["reference"] for r in ref_rows}
check("R1: program that needs std::macro is listed, not an invalid reference",
      (by["a/needs_macro.cml"], by["a/broken.cml"], by["a/never.cml"], by["a/grad.cml"]),
      ("REQUIRES_STD_MACRO", "INVALID_REF", "INVALID_REF", "REFERENCE"))
check("R1 off: the same program is an invalid reference",
      {r["program"]: r["reference"] for r in c.classify_all(recs, N, 20.0, 10.0, none, None)[1]}["a/needs_macro.cml"],
      "INVALID_REF")
cellrow = {r["pipeline_id"]: r for r in rows}
check("R1: row columns", (cellrow["P11"]["outcome"], cellrow["P11"]["defect_class"], cellrow["P11"]["std_macro_in_pipeline"],
                          cellrow["P1"]["defect_class"], cellrow["P4"]["defect_class"]),
      ("LIBRARY_PRECONDITION", "library_precondition", "no std::macro in the pipeline", "crash_hang_or_error", "agrees"))
check("R1: unknown pass is a refusal of a command line, still unlocated (D2 kept)",
      [(r["outcome"], r["refusal_subject"], r["defect_class"]) for r in refusal_rows],
      [("REFUSED_UNLOCATED", "command line (no source construct to locate)", "diagnostic_unlocated")])
table = {t["pass"]: t for t in c.standard_pass_table(rows)}
check("R1: per-pass table counts defect cells, not library preconditions",
      (table["std::nvm"]["cells"], table["std::nvm"]["defect_cells"], table["std::nvm"]["library_precondition_cells"],
       table["std::opt"]["defect_cells"], table["std::macro"]["defect_cells_in_pipelines_without_it"]),
      (3, 1, 1, 0, 1))
sig, causes = c.signature_tables(rows, refusal_rows, True)
check("R1: library precondition has its own set in the signature tables",
      sorted((t["set"] == "agree set", t["cells"]) for t in causes if t["set"].startswith("agree set")),
      [(False, 1), (True, 1)])
c.ACTIVE, c.STRIP_PAYLOAD_TYPES = None, False

bad = [x for x in checks if not x[3]]
for name, got, want, ok in checks:
    print(("ok  " if ok else "FAIL"), name, "->", got, "" if ok else f"(wanted {want})")
print(f"{len(checks) - len(bad)} / {len(checks)} checks passed")
sys.exit(1 if bad else 0)
