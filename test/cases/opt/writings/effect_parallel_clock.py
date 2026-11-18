"""Item (c) of the ECOOP measurement plan (2026-11-18): do two unrelated effects run at once?

The question as the plan puts it: "two effects that touch different resources (say two different
files), on the parallel back end against the serial one, wall clock, several runs each, median and
range". This script is that measurement, and the numbers it prints are the reading whether or not the
answer is the one the question expected.

The program is generated here (a `.cml` file in the tree would move the front-end AST golden for a
reason that has nothing to do with the front end -- `test/cases/trans/same_graph_writings.py` states
the same reason). It is one function that writes one file, delayed by a sleep so that the wall clock
shows whether two of them took turns:

    func write_late(path: string, text: string, ms: int): int sync {
        sleep(float(ms) / 1000.0)
        let _ = text->write_text<path>
        return 0
    }

and an entry that calls it twice, once per file. One sleep is 200 ms, so the two readings are far
apart and neither can be confused with the other by timing noise:

    took turns        about 400 ms on both back ends
    overlapped        about 200 ms on the parallel one, 400 ms on the serial one

**A wall-clock measurement is not a verdict, so this script has none.** It prints the median and the
range of N runs per back end, alternating the two so that a machine that slows down halfway through
slows both readings alike, and states which of the two shapes the numbers have. No plan entry runs it:
a bound on wall clock in the fast gate is a flake, and what the plan of 2026-11-18 asks for here is a
reading, not a red.

Run: python test/cases/opt/writings/effect_parallel_clock.py --camel out/latest/bin/camel [--runs 9]
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

PROGRAM = """import {{ now }} from time
import {{ sleep }} from process
import {{ write_text }} from fs

// One effect on one file, after a delay long enough to see whether two of these took turns.
func write_late(path: string, text: string, ms: int): int sync {{
    sleep(float(ms) / 1000.0)
    let _ = text->write_text<path>
    return 0
}}

func main(): int sync {{
    let first = write_late('effect_a.txt', 'a', {delay})
    let second = write_late('effect_b.txt', 'b', {delay})
    return first + second
}}
""".format(delay=DELAY_MS)

BACKENDS = (
    # (name, the flags that come before the program, the pipeline that comes after it)
    ("serial", [], ["std::nvm"]),
    ("parallel", ["-t", THREADS], ["std::prl"]),
)


def say(text=""):
    print(text)
    sys.stdout.flush()


def run_once(scratch, program, flags, passes):
    """One run of the program under one back end: its wall clock in milliseconds, and its exit."""
    started = time.monotonic()
    run = subprocess.run(
        [EXE, *flags, str(program), *passes],
        cwd=str(scratch),
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    elapsed = (time.monotonic() - started) * 1000.0
    if run.returncode != 0:
        raise SystemExit(
            f"camel {' '.join([*flags, 'prog.cml', *passes])} = exit {run.returncode}: "
            f"{((run.stderr or run.stdout or '').strip().splitlines() or [''])[0]}"
        )
    return elapsed


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
    program = scratch / "two_effects.cml"
    program.write_text(PROGRAM, encoding="utf-8", newline="\n")

    say(f"program: {program}")
    say(f"two effects, two different files, each after a {DELAY_MS} ms sleep; "
        f"{RUNS} runs per back end, alternating serial and parallel")
    say()

    samples = {name: [] for name, _, _ in BACKENDS}
    for _ in range(RUNS):
        for name, flags, passes in BACKENDS:
            samples[name].append(run_once(scratch, program, flags, passes))

    say("wall clock")
    medians = {name: describe(name, samples[name]) for name, _, _ in BACKENDS}
    serial, parallel = medians["serial"], medians["parallel"]
    say()
    say(f"median serial / median parallel = {serial / parallel:.2f}")

    # What shape the numbers have. The two answers are 200 ms apart, which is fifty times the
    # spread this machine shows between runs, so the comparison is read off the middle of each set
    # and not off a bound.
    if parallel < serial * 0.75:
        say("reading: the two effects OVERLAPPED under the parallel back end: the wall clock is one "
            "sleep there and two in series here")
        say("reading: two writes to two different files are two resources the scheduler may keep "
            "apart")
    else:
        say("reading: the two effects TOOK TURNS on both back ends")
        say("reading: the wall clock is the sum of the two sleeps on either, so two writes to two "
            "different files are one resource to the scheduler, ordered one against the other")

    # The scheduler's own word, once: which graphs it kept serial and what it cut into tasks. It is
    # the reason behind the numbers above, and it is read here instead of inferred from them.
    say()
    say("the parallel back end's decision for the program (std::prl diag=true):")
    run = subprocess.run(
        [EXE, "-t", THREADS, str(program), "std::prl", "diag=true"],
        cwd=str(scratch),
        capture_output=True,
        text=True,
        env=CHILD_ENV,
    )
    text = ((run.stdout or "") + (run.stderr or "")).strip().splitlines()
    for line in text[:40]:
        say(f"  {line}")
    return 0


if __name__ == "__main__":
    sys.exit(main())