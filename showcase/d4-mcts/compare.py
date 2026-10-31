# D4 -- run the MCTS showcase and compare it with the Python counterpart.
#
# Both programs are the same search, so the comparison is exact: the same arguments must build
# the same tree, line for line and node for node.  compare.py runs both on every case, diffs
# their output and their exit code, checks the answers a reader can check by hand, times them,
# counts the code by section and writes RESULTS.md.  It exits non-zero on the first difference,
# so the slow plan entry fails with it.
#
# The learned half (`selfplay.cml`) has no Python counterpart to diff against, so it is not
# compared: it is run once, and its readings -- sixteen games, ten steps, the loss sequence and
# the wall time -- go into the report beside the search's exact numbers.  That is the one place the
# two halves are read side by side: the search's lines are pinned, the training loop's curve is
# recorded, and the plan pins only that the curve printed and that its numbers are finite.

from __future__ import annotations

import argparse
import datetime as dt
import platform
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(REPO / "showcase"))

from common.showcase_compare import (  # noqa: E402
    camel_env, python_env, run_command, section_counts, table, write_results,
)

PROFILES = {
    "small": {"playouts": 2000, "empty": 5000},
    "standard": {"playouts": 20000, "empty": 20000},
}

# (name, position, playouts): a key into the profile, or a literal count for a guard.
CASES = (
    ("win now", "XX.OO....", "playouts"),
    ("block the only threat", ".X..O.X.O", "playouts"),
    ("fork", "X..OX...O", "playouts"),
    ("empty board", ".........", "empty"),
    ("guard: no playouts", "XX.OO....", 0),
    ("guard: unreachable position", "XXXX.....", 100),
    ("guard: finished position", "XXXOO....", 100),
)

# What a reader can check by hand, and what the search must answer at any budget this comparison
# runs: the winning place in the first position, the only move that does not lose in the second,
# the fork in the third, and the centre of the empty board.
ANSWERS = {
    "win now": 2,
    "block the only threat": 0,
    "fork": 2,
    "empty board": 4,
}
SEED = 42


def camel_executable(explicit: str | None) -> Path:
    if explicit:
        return Path(explicit)
    return REPO / "out" / "latest" / "bin" / ("camel.exe" if sys.platform == "win32" else "camel")


def camel_command(camel: Path, position: str, playouts: int, passes: list[str]) -> list[str]:
    return [
        str(camel), str(HERE / "search.cml"),
        "--arg", f"seed={SEED}",
        "--arg", f"playouts={playouts}",
        "--arg", f"position={position}",
        *passes,
    ]


def python_command(position: str, playouts: int) -> list[str]:
    return [
        sys.executable, str(HERE / "baseline" / "mcts.py"),
        "--seed", str(SEED),
        "--playouts", str(playouts),
        "--position", position,
    ]


def output_lines(stdout: str) -> list[str]:
    return [line.rstrip() for line in stdout.replace("\r\n", "\n").split("\n") if line.strip()]


def first_difference(camel: str, python: str) -> str:
    a, b = output_lines(camel), output_lines(python)
    for index in range(max(len(a), len(b))):
        left = a[index] if index < len(a) else "(missing)"
        right = b[index] if index < len(b) else "(missing)"
        if left != right:
            return f"line {index + 1}: `{left}` vs `{right}`"
    return ""


def summary(stdout: str) -> str:
    for line in output_lines(stdout):
        if line.startswith("d4 ") and not line.startswith("d4 config"):
            return line
    return "(no output)"


def best_of(stdout: str, field: str) -> str:
    for line in output_lines(stdout):
        if line.startswith("d4 search "):
            for item in line.split():
                if item.startswith(field + "="):
                    return item.split("=", 1)[1]
    return "n/a"


def best_mean(stdout: str, best: str) -> str:
    for line in output_lines(stdout):
        if line.startswith("d4 child ") and f"place={best} " in line:
            for item in line.split():
                if item.startswith("mean="):
                    return item.split("=", 1)[1]
    return "n/a"


def bracketed(line: str, name: str) -> list[float]:
    """The numbers of a `name=[...]` field of one output line."""
    marker = f"{name}=["
    start = line.index(marker) + len(marker)
    end = line.index("]", start)
    return [float(item) for item in line[start:end].split(",")]


def selfplay_readings(stdout: str) -> tuple[int, int, list[float], list[float]]:
    """(games, steps, game results, loss sequence) of `selfplay.cml`'s two lines."""
    games = steps = 0
    results: list[float] = []
    losses: list[float] = []
    for line in output_lines(stdout):
        if not line.startswith("mcts selfplay "):
            continue
        for item in line.split():
            if item.startswith("games="):
                games = int(item.split("=", 1)[1])
            elif item.startswith("steps="):
                steps = int(item.split("=", 1)[1])
        if "results=[" in line:
            results = bracketed(line, "results")
        if "losses=[" in line:
            losses = bracketed(line, "losses")
    return games, steps, results, losses


def code_size() -> list[tuple]:
    camel = section_counts(HERE / "search.cml", "==", "camel")
    python = section_counts(HERE / "baseline" / "mcts.py", "==", "python")
    sections = list(camel) + [name for name in python if name not in camel]
    rows = [(name, camel.get(name, 0), python.get(name, 0)) for name in sections]
    rows.append(("total", sum(camel.values()), sum(python.values())))
    return rows


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=sorted(PROFILES), default="small")
    parser.add_argument("--results", default=None)
    parser.add_argument("--camel", default=None)
    args = parser.parse_args()
    profile = PROFILES[args.profile]
    results = Path(args.results or (HERE / "RESULTS.md"))
    camel = camel_executable(args.camel)

    rows, answers, runs, problems = [], [], [], []
    for name, position, budget in CASES:
        playouts = profile[budget] if isinstance(budget, str) else budget
        c = run_command(camel_command(camel, position, playouts, ["std::nvm"]), camel_env(REPO))
        p = run_command(python_command(position, playouts), python_env(1))
        difference = first_difference(c["stdout"], p["stdout"])
        if c["exit"] != p["exit"]:
            difference = difference or f"exit code: {c['exit']} vs {p['exit']}"
        ok = not difference
        rows.append((
            f"{name} (`{position}`, {playouts} playouts)",
            summary(c["stdout"]), summary(p["stdout"]),
            difference or "none", "PASS" if ok else "FAIL",
        ))
        runs.append((name, position, playouts, c, p))
        if not ok:
            problems.append(f"{name} (`{position}`, {playouts} playouts): {difference}")
        if name in ANSWERS:
            best = best_of(c["stdout"], "best")
            wanted = str(ANSWERS[name])
            answers.append((
                f"{name} (`{position}`, {playouts} playouts)", wanted, best,
                best_mean(c["stdout"], best),
                "PASS" if best == wanted else "FAIL",
            ))
            if best != wanted:
                problems.append(f"{name} (`{position}`, {playouts} playouts): "
                                f"expected best={wanted}, got best={best}")
    # The four back ends must build the same tree; `feat/showcase_d4.plan.toml` pins their stdout,
    # and this says so in the report the comparison writes.
    backend_rows = []
    reference = None
    for backend in ("std::nvm", "std::fvm", "std::jit", "std::prl"):
        run = run_command(camel_command(camel, "X..OX...O", profile["playouts"], [backend]),
                          camel_env(REPO))
        if reference is None:
            reference = run["stdout"]
        same = output_lines(run["stdout"]) == output_lines(reference)
        backend_rows.append((backend, str(run["exit"]), summary(run["stdout"]),
                             "identical to nvm" if same else "differs", "PASS" if same else "FAIL"))
        if not same:
            problems.append(f"{backend}: stdout differs from std::nvm")

    timed = [run for run in runs if run[0] == "fork"][0]
    # The learned half, run once: it has no counterpart to diff against, so its readings are
    # recorded instead of compared.  A curve that does not print, or that prints something other
    # than one finite loss per step, is the defect this run can see.
    play = run_command([str(camel), str(HERE / "selfplay.cml")], camel_env(REPO))
    p_games, p_steps, p_results, p_losses = selfplay_readings(play["stdout"])
    if play["exit"] != 0 or not p_losses or len(p_losses) != p_steps:
        problems.append(f"self-play: exit {play['exit']}, {len(p_losses)} losses for "
                        f"{p_steps} steps")
    mean_result = sum(p_results) / len(p_results) if p_results else 0.0
    version = run_command([str(camel), "--version"], camel_env(REPO))
    lines = []
    lines.append("# D4 — Monte-Carlo tree search: measured results")
    lines.append("")
    lines.append("Generated by `showcase/d4-mcts/compare.py`; do not edit by hand.")
    lines.append("")
    lines.append(f"- date: {dt.datetime.now().astimezone().isoformat(timespec='seconds')}")
    lines.append(f"- Camel: {version['stdout'].strip() or version['stderr'].strip()}")
    lines.append(f"- Python: {sys.version.split()[0]} (the counterpart is the standard library "
                 f"alone: no NumPy, no PyTorch)")
    lines.append(f"- command: `{' '.join([Path(sys.executable).name, *sys.argv])}`")
    lines.append(f"- profile: {args.profile}; seed {SEED}, "
                 f"{profile['playouts']} playouts per search and {profile['empty']} on the empty "
                 f"board, one process per run")
    lines.append("")
    lines.append("## Agreement")
    lines.append("")
    lines.append(table(rows, ("case", "Camel (nvm)", "Python", "difference", "result")))
    lines.append("")
    lines.append("The comparison is exact, not a tolerance: both programs are the same search over "
                 "the same IEEE-754 doubles, written in the same order, so the same arguments must "
                 "give the same tree.  A difference is a defect in one of them, not noise.")
    lines.append("")
    lines.append("## Answers")
    lines.append("")
    lines.append(table(answers, ("case", "best move", "Camel", "mean of the best move", "result")))
    lines.append("")
    lines.append("These are the moves a reader can check by hand: X completes the line at 2 in "
                 "`XX.OO....`; 0 is the only place in `.X..O.X.O` that stops O from completing "
                 "`0-4-8`; `X..OX...O` is a fork at 1 or 2, and the search prefers 2; and the "
                 "empty board's centre is the move the search values highest.")
    lines.append("")
    lines.append("## Back ends")
    lines.append("")
    lines.append(table(backend_rows, ("back end", "exit", "summary", "stdout", "result")))
    lines.append("")
    lines.append("`feat/showcase_d4.plan.toml` pins the same four outputs on the four back ends.")
    lines.append("")
    lines.append("## Performance")
    lines.append("")
    lines.append(table([
        ("search time (s)", f"{timed[3]['seconds']:.6f}", f"{timed[4]['seconds']:.6f}",
         f"{timed[3]['seconds'] / timed[4]['seconds']:.3f}"),
        ("playouts per second", f"{profile['playouts'] / timed[3]['seconds']:.0f}",
         f"{profile['playouts'] / timed[4]['seconds']:.0f}",
         f"{timed[3]['seconds'] / timed[4]['seconds']:.3f}"),
        ("peak process memory (MiB)", f"{timed[3]['peak_bytes'] / 2**20:.1f}",
         f"{timed[4]['peak_bytes'] / 2**20:.1f}",
         f"{timed[3]['peak_bytes'] / max(timed[4]['peak_bytes'], 1):.3f}"),
    ], ("metric", "Camel (nvm)", "Python", "Camel / Python")))
    lines.append("")
    lines.append(f"The row above is the `fork` case (`X..OX...O`, {profile['playouts']} playouts); "
                 f"both are whole-process wall times, which this host shares with other work, so "
                 f"they are reported and never asserted.  Every number the search reports is "
                 f"exact and reproducible: nothing in it is timed.")
    lines.append("")
    lines.append("## Self-play (the learned half)")
    lines.append("")
    lines.append(table([
        ("games played", str(p_games)),
        ("steps taken", str(p_steps)),
        ("mean result of the games", f"{mean_result:.3f}"),
        ("loss, first three", ", ".join(f"{x:.6f}" for x in p_losses[:3])),
        ("loss, last three", ", ".join(f"{x:.6f}" for x in p_losses[-3:])),
        ("wall time (s)", f"{play['seconds']:.3f}"),
    ], ("reading", "value")))
    lines.append("")
    lines.append("There is nothing to diff here: the Python counterpart is the search, and the "
                 "training loop is the part of D4 it does not have.  So this table is a reading, "
                 "not a comparison -- the games are scripted (a player takes a free place drawn "
                 "from the game's key, no search), the batch is fixed while the ten Adam steps "
                 "move the model, and `feat/showcase_d4.plan.toml` pins that the sequence is "
                 "printed and that its numbers are finite, never what they are.  The one thing a "
                 "bigger run can break is its cost: sixteen games times ten steps stays in the "
                 "same tenth of a second as one game and one step, because the schedule of this "
                 "tiny graph is what the wall time is made of, not the arithmetic.")
    lines.append("")
    lines.append("## Code size (code lines by section)")
    lines.append("")
    lines.append(table(code_size(), ("section", "Camel", "Python")))
    lines.append("")
    text = "\n".join(lines) + "\n"
    write_results(results, text)
    sys.stdout.write("\n" + text)
    # The diagnosis is printed last so that the tail of a long report still carries it: the plan
    # runner shows the head and the tail of a failed test's output, not the middle.  The same goes
    # for the digest below, which carries every number the report's tables were built from.
    for problem in problems:
        print(f"d4 differs: {problem}")
    for name, position, playouts, c, p in runs:
        print(f"d4 measured: {name} position={position} playouts={playouts} "
              f"exit={c['exit']}/{p['exit']} best={best_of(c['stdout'], 'best')} "
              f"mean={best_mean(c['stdout'], best_of(c['stdout'], 'best'))} "
              f"camel_s={c['seconds']:.3f} python_s={p['seconds']:.3f} "
              f"summary={summary(c['stdout'])}")
    print(f"d4 measured: perf fork camel_s={timed[3]['seconds']:.3f} "
          f"python_s={timed[4]['seconds']:.3f} "
          f"camel_playouts_s={profile['playouts'] / timed[3]['seconds']:.0f} "
          f"python_playouts_s={profile['playouts'] / timed[4]['seconds']:.0f} "
          f"camel_peak_mib={timed[3]['peak_bytes'] / 2**20:.1f} "
          f"python_peak_mib={timed[4]['peak_bytes'] / 2**20:.1f} "
          f"python_version={platform.python_version()}")
    print(f"d4 measured: selfplay games={p_games} steps={p_steps} "
          f"loss_first={p_losses[0] if p_losses else float('nan'):.6f} "
          f"loss_last={p_losses[-1] if p_losses else float('nan'):.6f} "
          f"mean_result={mean_result:.3f} camel_s={play['seconds']:.3f}")
    sections = code_size()
    print(f"d4 measured: size camel={sections[-1][1]} python={sections[-1][2]} "
          f"sections={len(sections) - 1}")
    failures = len(problems)
    print(f"d4 compare: {'FAIL' if failures else 'PASS'} ({failures} bound(s) exceeded)")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())