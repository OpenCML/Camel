import hashlib
import json
import subprocess
import sys

# Deliberately exits 1: a failing entry's stdout is echoed back, and this probe's reading IS its
# output. `std::stats` runs BEFORE the dot-printing pass, so its record describes the graph that
# gets printed; its line is then removed from the dot text, so the bytes are the dot alone.
print("driver.self_sha256=%s" % hashlib.sha256(open(__file__, "rb").read()).hexdigest())
exe = sys.argv[1]
case = sys.argv[2]
stage = sys.argv[3]
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
    cmd = [exe, case] + passes + ["std::stats", "std::gir", "std::nop"]
    p = subprocess.run(cmd, capture_output=True, text=True)
    dot, stats = [], None
    for line in p.stdout.splitlines():
        if line.startswith("GRAPH_STATS "):
            stats = json.loads(line[len("GRAPH_STATS "):])
        else:
            dot.append(line)
    text = "\n".join(dot)
    return {
        "cmd": " ".join(cmd),
        "rc": p.returncode,
        "bytes": len(text),
        "sha256": hashlib.sha256(text.encode()).hexdigest(),
        "stats": stats if isinstance(stats, dict) else {},
        "text": text,
        "stderr": (p.stderr.splitlines()[:2] if p.returncode != 0 else []),
    }

seen = {}
print("case=%s stage=%s" % (case, stage))
for route, passes in ROUTES[stage]:
    m = measure(passes)
    seen[route] = m["text"]
    print("cmd[%s]=%s" % (route, m["cmd"]))
    print("route=%s rc=%d bytes=%d sha256=%s" % (route, m["rc"], m["bytes"], m["sha256"]))
    s = m["stats"]
    print("keys=%s" % ",".join(sorted(s.keys())))
    print("stats[%s]=nodes:%s edges:%s direct_calls:%s graphs:%s bytes:%s"
          % (route, s.get("nodes", "-"), s.get("edges", "-"), s.get("direct_calls", "-"),
             s.get("graphs", "-"), s.get("bytes", "-")))
    if m["stderr"]:
        print("route=%s stderr=%s" % (route, m["stderr"]))
if len(seen) == 2:
    print("identical=%s" % (seen["camel"] == seen["cpp"]))
sys.exit(1)
