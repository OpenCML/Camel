#!/usr/bin/env python3
"""Synthetic checks of classify.py: under the frozen rules, payload comparison by value and its
hash fallback, tolerances, the benchmark timing mask, the time limit, written files; under the
newest rule set, the deviations D2 (unlocated refusals), D4 (exclusion list), D5 (must-refuse
pruning) and D7 (file-writing operators compared through written content).

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
N = c.resolve_rules(c.NEWEST)
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
check("newest has every flag", (N.unlocated_refusal_violation, N.exclusion_list, N.must_refuse_static_only,
                                N.file_ops_by_content, N.report_v2), (True,) * 5)
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
check("D10 (proposed): full comparison differs", outcome_under(P10, b0, big("b", [1.0, 2.0], differs))["outcome"],
      "VALUE_MISMATCH")
check("D10 (proposed): full comparison within tolerance",
      (lambda i: (i["outcome"], i["file_sampled"]))(outcome_under(P10, b0, big("b", [1.0, 2.0001], within))),
      ("CONSISTENT", False))
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

bad = [x for x in checks if not x[3]]
for name, got, want, ok in checks:
    print(("ok  " if ok else "FAIL"), name, "->", got, "" if ok else f"(wanted {want})")
print(f"{len(checks) - len(bad)} / {len(checks)} checks passed")
sys.exit(1 if bad else 0)
