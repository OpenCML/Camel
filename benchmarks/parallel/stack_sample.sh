#!/bin/bash
# Poor man's profiler for a Camel run, for containers without `perf` (and where gdb may not
# attach to a running process): runs the command under gdb, interrupts it every INTERVAL seconds
# and counts in which function each thread was found. The run is killed after SAMPLES samples.
#
#   benchmarks/parallel/stack_sample.sh [-n SAMPLES] [-i INTERVAL] [-d DEPTH] -- camel prog.cml std::prl
#
# Prints, over all samples and threads, how often each innermost frame was seen ("self"), and how
# often each function was anywhere in the first DEPTH frames ("in stack"). A thread parked in
# the executor shows up as a futex wait under tf::Executor; a thread waiting for a lock as
# __lll_lock_wait / pthread_mutex_lock. Sampling stops the process for a few milliseconds each
# time: the run takes longer than unsampled, the proportions are what to read.
set -u
samples=40; interval=0.05; depth=6
while [ $# -gt 0 ]; do
    case $1 in
        -n) samples=$2; shift 2 ;;
        -i) interval=$2; shift 2 ;;
        -d) depth=$2; shift 2 ;;
        --) shift; break ;;
        *) break ;;
    esac
done
[ $# -gt 0 ] || { echo "usage: $0 [-n N] [-i SEC] [-d DEPTH] -- command..." >&2; exit 2; }
tmp=$(mktemp -d)
cat > "$tmp/sampler.py" <<'PY'
import os
import gdb
samples = int(os.environ["SS_SAMPLES"]); depth = os.environ["SS_DEPTH"]
out = open(os.environ["SS_OUT"], "w")
for cmd in ("set pagination off", "set confirm off", "set print thread-events off"):
    gdb.execute(cmd)
gdb.execute("starti", to_string=True)
# The shell loop below interrupts this process; gdb stops the program on SIGINT and does not
# pass the signal on. (A Python timer thread cannot do it: gdb keeps the interpreter lock
# while the program runs.)
open(os.environ["SS_OUT"] + ".pid", "w").write(str(gdb.selected_inferior().pid))
taken = 0
while taken < samples:
    try:
        gdb.execute("continue", to_string=True)
    except gdb.error:
        break
    if gdb.selected_inferior().pid == 0:
        break
    out.write(gdb.execute("thread apply all bt " + depth, to_string=True))
    taken += 1
out.write("\n")
out.close()
open(os.environ["SS_OUT"] + ".n", "w").write(str(taken))
if gdb.selected_inferior().pid != 0:
    gdb.execute("kill")
PY
(
    while [ ! -s "$tmp/stacks.pid" ]; do sleep 0.01; done
    pid=$(cat "$tmp/stacks.pid")
    sleep "${WARMUP:-0.1}"
    while kill -INT "$pid" 2>/dev/null; do sleep "$interval"; done
) &
SS_SAMPLES=$samples SS_DEPTH=$depth SS_OUT="$tmp/stacks" \
    gdb -batch -x "$tmp/sampler.py" --args "$@" > "$tmp/out" 2>&1
wait
taken=$(cat "$tmp/stacks.n" 2>/dev/null || echo 0)
echo "# $taken samples of: $*"
python3 - "$tmp/stacks" <<'EOF'
import collections, re, sys
selfc, anyc, threads = collections.Counter(), collections.Counter(), 0
frames = []
def flush():
    global threads
    if not frames:
        return
    threads += 1
    selfc[frames[0]] += 1
    for name in set(frames):
        anyc[name] += 1
    frames.clear()
def name_of(line):
    m = re.match(r"#\d+\s+(?:0x[0-9a-f]+ in )?(.*?) \(", line)
    if not m:
        return None
    name = re.sub(r"\(.*", "", m.group(1))
    name = re.sub(r"<.*>", "<>", name)
    return name[-70:]
try:
    for line in open(sys.argv[1], errors="replace"):
        if line.startswith("Thread "):
            flush()
        elif line.startswith("#"):
            n = name_of(line)
            if n:
                frames.append(n)
    flush()
except FileNotFoundError:
    pass
print(f"# {threads} thread stacks")
for title, counter in (("self", selfc), ("in stack", anyc)):
    print(f"## {title}")
    for name, count in counter.most_common(18):
        print(f"{100.0 * count / max(threads, 1):6.1f}%  {name}")
EOF
rm -rf "$tmp"
