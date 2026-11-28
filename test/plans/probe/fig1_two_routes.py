import hashlib
import json
import subprocess
import sys

# Deliberately exits 1: a failing entry's stdout is what the runner echoes back, and this probe
# has no assertion to make -- its reading IS its output. The driver names its own hash so the
# reading says which copy ran on the host, not which copy was on the lane's disk.
print("driver.self_sha256=%s" % hashlib.sha256(open(__file__, "rb").read()).hexdigest())
exe = sys.argv[1]
case = sys.argv[2]
stage = sys.argv[3]
TAIL = ["std::gir", "std::nop"]

ROUTES = {
    "before": [("camel", [])],
    "inline": [("camel", ["opt::inline"]), ("cpp", ["std::cxx::inline"])],
    "inline_cse": [("camel", ["opt::inline", "opt::cse"]), ("cpp", ["std::cxx::inline", "opt::cse"])],
    "inline_cse_algebra": [
        ("camel", ["opt::inline", "opt::cse", "opt::algebra"]),
        ("cpp", ["std::cxx::inline", "opt::cse", "opt::algebra"]),
    ],
}

def measure(passes):
    cmd = [exe, case] + passes + TAIL + ["std::stats"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    dot, stats = [], None
    for line in p.stdout.splitlines():
        if line.startswith("GRAPH_STATS "):
            stats = json.loads(line[len("GRAPH_STATS "):])
        else:
            dot.append(line)
    text = "\n".join(dot)
    gs = (stats or {}).get("graphs") or []
    counts = "nodes=%d edges=%d direct_calls=%d" % (
        sum(int(g.get("nodes", 0)) for g in gs),
        sum(int(g.get("edges", 0)) for g in gs),
        sum(int(g.get("direct_calls", 0)) for g in gs),
    )
    return {
        "cmd": " ".join(cmd),
        "rc": p.returncode,
        "bytes": len(text),
        "sha256": hashlib.sha256(text.encode()).hexdigest(),
        "counts": counts,
        "text": text,
        "err": (p.stderr.splitlines()[:2] if p.returncode != 0 else []),
    }

seen = {}
print("case=%s stage=%s" % (case, stage))
for route, passes in ROUTES[stage]:
    m = measure(passes)
    seen[route] = m["text"]
    print("cmd[%s]=%s" % (route, m["cmd"]))
    print("route=%s rc=%d bytes=%d sha256=%s %s" % (route, m["rc"], m["bytes"], m["sha256"], m["counts"]))
    if m["err"]:
        print("route=%s stderr=%s" % (route, m["err"]))
if len(seen) == 2:
    print("identical=%s" % (seen["camel"] == seen["cpp"]))
sys.exit(1)
