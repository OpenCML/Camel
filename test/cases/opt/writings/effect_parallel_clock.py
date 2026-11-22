"""Item (c) of the ECOOP measurement plan (2026-11-18): do two effects on two resources run at once?

The author's ruling after the first reading of this item (`1b7905cd4`): the measurement was on two
*writings* of one resource, not two resources, so it could not answer the question. This is the
measurement on **two resources the language declares apart**, and on what parts them.

The resources, as `stdlib` declares them:

  clock              `sleep` waits on it (`stdlib/process.cml:15`, `@effect waits`); the six standard
                     resources are named in `src/core/resource_semantics.cpp:504`:
                     `stdout stderr stdin clock env fs`
  rendezvous_point   `stdlib/test_support.cml:17`, `export resource rendezvous_point unordered`, the
                     one meeting point of the process: "two calls on it keep no written order and may
                     run at the same time (design principles 3.3)"

The program puts one 200 ms effect on each, in a `main` that is **not** a `sync` body:

    func wait_clock(ms: int): int sync {      // void effect, so in a `sync` body or it is dropped
        sleep(float(ms) / 1000.0)             // -- `fs.cml`: "a call runs only when its result is
        return 1                              //    needed or it is written in a `sync` block"
    }
    func wait_meet(ms: int): int {            // `unordered`, so no `sync` and no written order
        return rendezvous(2, float(ms) / 1000.0)
    }
    func main(): int {                        // not `sync`: the two calls are free to be unordered
        let a = wait_clock(ms)
        let b = wait_meet(ms)
        ...

A `sync` body is how the front end orders conflicting effects in the graph, so `sync` in `main` would
be the thing that parts them; that is why `main` here is not one, and why the script also runs the
control below.

**The control is the same two calls with `sync` on and off, and it is a reading of values, not of
seconds.** `rendezvous(2, t)` gives how many calls met at the meeting point: two calls that run at the
same time both report 2, two that took turns report 1 and 1. So `sync` on against `sync` off on one
unordered resource says whether the wall clock above is about resources at all.

The window `t` is where a second of wall clock does enter, and it is the one thing here a loaded
host can starve: a call that reports 1 has either been ordered after the other call or arrived
after the window closed, and the two causes look the same at 200 ms. So the pair is read twice, at
`DELAY_MS` and at ten times that. A pair the scheduler ordered reports 1 at *every* window, because
a call that waits gives up only when the window closes; a pair the host starved reports 2 as soon
as the window is wide enough. The narrow cell is the host reading, the wide cell is the scheduler
reading, and a scheduler that is free of `sync` and still orders the two calls fails the wide cell
(asked three times) and this case with it.

**No verdict, no bound.** It prints medians, ranges, ratios and the scheduler's own decision, and
names which shape the numbers have. Run:

    python test/cases/opt/writings/effect_parallel_clock.py --camel out/latest/bin/camel [--runs 9]
"""

import os
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent


def repository_root(start):
    """The first ancestor holding `stdlib/std.cml`: neither cwd nor `HERE` is the root."""
    for directory in [start, *start.parents]:
        if (directory / "stdlib" / "std.cml").is_file():
            return directory
    raise SystemExit(f"no stdlib/std.cml above {start}")


ROOT = repository_root(HERE)
sys.path.insert(0, str(ROOT / "test" / "tools"))
from camel_arg import camel_from_argv  # noqa: E402

EXE, ARGV = camel_from_argv(ROOT)
RUNS = 9
for index, word in enumerate(ARGV):
    if word == "--runs" and index + 1 < len(ARGV):
        RUNS = int(ARGV[index + 1])

DELAY_MS = 200
THREADS = "4"

# One effect on the clock, one on the meeting point. `main` is not `sync`.
TWO_RESOURCES = """import {{ sleep }} from process
import {{ rendezvous }} from test_support

func wait_clock(ms: int): int sync {{
    sleep(float(ms) / 1000.0)
    return 1
}}

func wait_meet(ms: int): int {{
    return rendezvous(2, float(ms) / 1000.0)
}}

func main(): int {{
    let a = wait_clock({delay})
    let b = wait_meet({delay})
    print(a * 1000 + b)
    return 0
}}
""".format(delay=DELAY_MS)

# The control: one unordered resource, the same two calls, `sync` on (the two callees and the caller)
# against `sync` off. Nothing about seconds here -- the values say how many calls met.
SYNC_PAIR = """import {{ rendezvous }} from test_support

func meet(ms: int): int sync {{
    return rendezvous(2, float(ms) / 1000.0)
}}

func main(): int sync {{
    let a = meet({delay})
    let b = meet({delay})
    print(a * 1000 + b)
    return 0
}}
""".format(delay=DELAY_MS)

FREE_PAIR = """import {{ rendezvous }} from test_support

func meet(ms: int): int {{
    return rendezvous(2, float(ms) / 1000.0)
}}

func main(): int {{
    let a = meet({delay})
    let b = meet({delay})
    print(a * 1000 + b)
    return 0
}}
""".format(delay=DELAY_MS)

# The same `free` pair with a window wide enough that a stalled host cannot starve it. The count is
# an answer about the schedule, not about seconds: two calls the scheduler put on two threads meet
# at the point and both report 2, and two it ordered one after the other report 1 -- at *every*
# window, because a serialized call waits the whole window out before it gives up. So a wide window
# cannot turn an ordered pair into a met one; it only keeps a host that stalled for longer than
# DELAY_MS from being read as a scheduler that ordered the two calls.
WIDE_MS = DELAY_MS * 10

FREE_PAIR_WIDE = """import {{ rendezvous }} from test_support

func meet(): int {{
    return rendezvous(2, {window})
}}

func main(): int {{
    let a = meet()
    let b = meet()
    print(a * 1000 + b)
    return 0
}}
""".format(window=f"{WIDE_MS / 1000.0}")

BACKENDS = (
    # (name, the flags that come before the program, the pipeline that comes after it)
    ("serial", [], ["std::nvm"]),
    ("parallel", ["-t", THREADS], ["std::prl"]),
)


def say(text=""):
    print(text)
    sys.stdout.flush()


def run(scratch, program, flags, passes):
    """One run: its wall clock in milliseconds, and what the program printed."""
    started = time.monotonic()
    done = subprocess.run(
        [EXE, *flags, str(program), *passes],
        cwd=str(scratch),
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    elapsed = (time.monotonic() - started) * 1000.0
    if done.returncode != 0:
        raise SystemExit(
            f"camel {' '.join([*flags, program.name, *passes])} = exit {done.returncode}: "
            f"{((done.stderr or done.stdout or '').strip().splitlines() or [''])[0]}"
        )
    printed = (done.stdout or "").strip().splitlines()
    return elapsed, (printed[-1].strip() if printed else "")


def describe(name, samples):
    ordered = sorted(samples)
    median = statistics.median(ordered)
    say(
        f"  {name:<9} n={len(ordered)} min={ordered[0]:8.1f} ms  median={median:8.1f} ms  "
        f"max={ordered[-1]:8.1f} ms   runs={', '.join(f'{one:.0f}' for one in samples)}"
    )
    return median


def main():
    global CHILD_ENV
    scratch = Path(os.environ.get("CAMEL_TEST_TMPDIR") or tempfile.mkdtemp(prefix="effect-clock-"))
    scratch.mkdir(parents=True, exist_ok=True)
    CHILD_ENV = dict(os.environ)
    CHILD_ENV.setdefault("CAMEL_HOME", str(Path(EXE).resolve().parent.parent))

    pair = scratch / "two_resources.cml"
    pair.write_text(TWO_RESOURCES, encoding="utf-8", newline="\n")

    say(f"program: {pair}")
    say("two resources: `clock` (sleep, stdlib/process.cml:15) and `rendezvous_point` "
        "(stdlib/test_support.cml:17, unordered)")
    say(f"one effect on each, {DELAY_MS} ms each, in a `main` that is not a `sync` body; "
        f"{RUNS} runs per back end, alternating serial and parallel")
    say()

    samples = {name: [] for name, _, _ in BACKENDS}
    printed = {}
    for _ in range(RUNS):
        for name, flags, passes in BACKENDS:
            elapsed, out = run(scratch, pair, flags, passes)
            samples[name].append(elapsed)
            printed[name] = out

    say("wall clock")
    medians = {name: describe(name, samples[name]) for name, _, _ in BACKENDS}
    serial, parallel = medians["serial"], medians["parallel"]
    say()
    say(f"median serial / median parallel = {serial / parallel:.2f}")
    say(f"the program printed: serial {printed['serial']}, parallel {printed['parallel']} "
        f"(a*1000+b: one call at the meeting point, so 1 either way)")

    # The shape of the numbers. The two answers are 200 ms apart, which is far more than this
    # machine's spread between runs, so the comparison is read off the middle of each set.
    if parallel < serial * 0.75:
        say("reading: the two effects OVERLAPPED: one wait on the parallel back end, two in series "
            "on the serial one")
        say("reading: `clock` and `rendezvous_point` are two resources the scheduler keeps apart")
    else:
        say("reading: the two effects TOOK TURNS on both back ends")
        say("reading: a wall clock of the sum of the two waits means the two effects were ordered "
            "one against the other")

    # The control: the same two calls on one unordered resource, `sync` on against `sync` off.
    say()
    say("control: two calls on `rendezvous_point` (unordered), `sync` on against `sync` off --")
    say("the number is a*1000+b, where each call reports how many calls met (2 = both at once):")
    control = {}
    for label, text in (("sync", SYNC_PAIR), ("free", FREE_PAIR)):
        path = scratch / f"pair_{label}.cml"
        path.write_text(text, encoding="utf-8", newline="\n")
        cells = []
        for name, flags, passes in BACKENDS:
            _, out = run(scratch, path, flags, passes)
            control[label, name] = out
            cells.append(f"{name} {out}")
        say(f"  {label:<6}{'   '.join(cells)}")

    # The `free` cell again with the wide window. A host that stalled for longer than DELAY_MS
    # reports 1 there for the same reason a `sync` body does, and those two causes have to be told
    # apart before the value means anything: the serialized pair reports 1 at this window too, so
    # this cell is the reading of the scheduler and the cell above is the reading of the host.
    wide_path = scratch / "pair_free_wide.cml"
    wide_path.write_text(FREE_PAIR_WIDE, encoding="utf-8", newline="\n")
    say()
    say(f"the same `free` pair with a {WIDE_MS} ms window ({WIDE_MS // DELAY_MS}x DELAY_MS):")
    wide = {}
    # The question is whether the back end *can* put the two calls on two threads, not whether it
    # did it once: a pair it ordered reports 1 on every attempt, so asking three times decides the
    # same thing for a scheduler that cannot and only forgives a host that was busy once.
    for _ in range(3):
        cells = []
        for name, flags, passes in BACKENDS:
            _, out = run(scratch, wide_path, flags, passes)
            wide[name] = out
            cells.append(f"{name} {out}")
        say(f"  free  {'   '.join(cells)}")
        if wide["parallel"] == "2002":
            break

    say()
    if control["free", "parallel"] == "2002":
        say("reading: two calls free of `sync` met on the parallel back end (both report 2)")
    elif wide["parallel"] == "2002":
        say(f"reading: the {DELAY_MS} ms window was missed on this host ({control['free', 'parallel']} "
            f"on the parallel back end); at {WIDE_MS} ms, two calls free of `sync` met on the "
            f"parallel back end")
    else:
        say(f"reading: two calls free of `sync` did NOT meet on the parallel back end, at the "
            f"{DELAY_MS} ms window or at the {WIDE_MS} ms one: the scheduler ordered them one "
            f"after the other")
        return 1

    # The scheduler's own word, once: which graphs it kept serial and what it cut into tasks. It is
    # the reason behind the numbers above, and it is read here instead of inferred from them.
    say()
    say("the parallel back end's decision for the program (std::prl diag=true):")
    done = subprocess.run(
        [EXE, "-t", THREADS, str(pair), "std::prl", "diag=true"],
        cwd=str(scratch),
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    text = (done.stderr or "").strip().splitlines()
    for line in text[:40]:
        say(f"  {line}")
    return 0
    done = subprocess.run(
        [EXE, "-t", THREADS, str(pair), "std::prl", "diag=true"],
        cwd=str(scratch),
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    text = (done.stderr or "").strip().splitlines()
    for line in text[:40]:
        say(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())