import hashlib
import json
import subprocess
import sys

OUT = "test/results/fig1-two-routes.txt"
lines = []

def emit(s):
    lines.append(s)
    print(s)

emit("driver.self_sha256=%s" % hashlib.sha256(open(__file__, "rb").read()).hexdigest())
emit("driver.argv=%s" % json.dumps(sys.argv))

exe = sys.argv[1]
case = sys.argv[2]
TAIL = ["std::gir", "std::nop"]

def run(passes):
    cmd = [exe, case] + passes + ["std::stats"]
    emit("cmd=%s" % " ".join(cmd))
    p = subprocess.run(cmd, capture_output=True, text=True)
    return p.returncode, p.stdout, p.stderr

def split_stats(out):
    dot = []
    stats = None
    for line in out.splitlines():
        if line.startswith("GRAPH_STATS "):
            stats = json.loads(line[len("GRAPH_STATS "):])
        else:
            dot.append(line)
    return "\n".join(dot), stats

def counts(stats):
    gs = (stats or {}).get("graphs") or []
    n = sum(int(g.get("nodes", 0)) for g in gs)
    e = sum(int(g.get("edges", 0)) for g in gs)
    c = sum(int(g.get("direct_calls", 0)) for g in gs)
    return "nodes=%d edges=%d direct_calls=%d" % (n, e, c)

stages = [
    ("before", [], None),
    ("inline", ["opt::inline"], ["std::cxx::inline"]),
    ("inline_cse", ["opt::inline", "opt::cse"], ["std::cxx::inline", "opt::cse"]),
    ("inline_cse_algebra", ["opt::inline", "opt::cse", "opt::algebra"],
                           ["std::cxx::inline", "opt::cse", "opt::algebra"]),
]

emit("case=%s" % case)
for name, camel_pre, cpp_pre in stages:
    rc1, out1, err1 = run(camel_pre + TAIL)
    t1, s1 = split_stats(out1)
    h1 = hashlib.sha256(t1.encode()).hexdigest()
    emit("stage=%s route=camel rc=%d bytes=%d sha256=%s %s" % (name, rc1, len(t1), h1, counts(s1)))
    if rc1 != 0:
        emit("stage=%s route=camel stderr=%s" % (name, err1.splitlines()[:2]))
    if cpp_pre is not None:
        rc2, out2, err2 = run(cpp_pre + TAIL)
        t2, s2 = split_stats(out2)
        h2 = hashlib.sha256(t2.encode()).hexdigest()
        emit("stage=%s route=cpp rc=%d bytes=%d sha256=%s %s" % (name, rc2, len(t2), h2, counts(s2)))
        if rc2 != 0:
            emit("stage=%s route=cpp stderr=%s" % (name, err2.splitlines()[:2]))
        emit("stage=%s identical=%s" % (name, t1 == t2))
emit("judge.done")

with open(OUT, "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("wrote=%s" % OUT)
