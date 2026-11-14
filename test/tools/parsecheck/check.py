#!/usr/bin/env python3
# Copyright (c) the OpenCML Organization
# SPDX-License-Identifier: MIT

"""The corpus checks of the front end, as the test plan runs them.

    python3 test/tools/parsecheck/check.py snapshot [--update] [--only GLOB ...]
    python3 test/tools/parsecheck/check.py format   [--only GLOB ...] [--allow-empty REASON]
    python3 test/tools/parsecheck/check.py fuzz     [ITERATIONS [SEED]] [--only GLOB ...]
                                                    [--allow-empty REASON]

snapshot  Every .cml case of the parse corpus (test/cases/parse), every probe (probes.py) and
          every other .cml file of the repository -- benchmarks, exemplars, examples, showcase,
          the standard library, the tutorial and the cases of the other plans -- is parsed; a
          hash of the normalized AST of each (or of its diagnostics' code and place, when it is
          rejected) is compared with test/plans/feat/parse/golden/ast_hashes.txt. The
          diagnostics are recorded by code and position and never by their wording, so rewording
          a message, a help text or a note leaves this file alone. The golden list was recorded
          when the hand-written parser was last compared, tree for tree, with the ANTLR parser it
          replaced; a line that changes means the front end reads that file differently.
          The record covers the repository and not only the parser's own tests, because a graph
          can change without a test noticing it: the standard library (the migration's own
          subject), the exemplars, the examples and the benchmarks are where a reading that moved
          hides, and a plan that owns the file only catches what it happens to assert. A .cml
          file under docs/handover is not in the corpus (corpus.HANDOVER): that directory holds
          the language author's drafts, not the repository's surface.
          --update merges into the recorded list: the selected lines are re-hashed and replaced,
          a file the list does not hold yet is hashed and added, every other line whose file
          still exists is kept, and a line whose file is gone -- or that the corpus no longer
          holds -- is dropped. The merged list must cover every case corpus.parse_cases() selects,
          every other file of the repository and every probe, or the update refuses and names the
          gaps. It merges rather than refusing a partial update because recording one changed case
          should not depend on re-reading the other 3000; the filter can then never shrink the
          record.
          --only GLOB restricts what is re-hashed, not what the record must cover; without it
          the whole corpus is refreshed.
format    Formatting every accepted file of the repository gives a text with the same AST, keeps
          every comment of the input, and is a fixed point (the text formats to itself). A file
          that loses a comment, or whose second pass differs, is named and fails the check.
fuzz      Mutated programs of the repository: the parser returns a lossless tree for each
          (camel-parsecheck fuzz).

A check that examines nothing is not a pass (agents/test.md). `format` and `fuzz` count the files
corpus.parse_cases() selects -- the parser's own corpus, counted from the tree on every run, never
written down -- and refuse a corpus smaller than that count. `--only` marks a deliberate subset
(down to one file, for a targeted run); an empty subset still needs `--allow-empty REASON`, which
is printed, so no run passes without saying what it examined. The same rule sits in the C++ tool:
hashes, fmtcheck and fuzz refuse an empty list (and fuzz refuses a list with no seed files).

Run from the repository root; the tools are those of out/latest unless CAMEL_HOME is set.
"""
import argparse
import fnmatch
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent.parent
GOLDEN = ROOT / "test" / "plans" / "feat" / "parse" / "golden" / "ast_hashes.txt"

sys.path.insert(0, str(HERE))
import corpus  # noqa: E402
import probes  # noqa: E402


def tool():
    home = Path(os.environ.get("CAMEL_HOME", ROOT / "out" / "latest"))
    name = "camel-parsecheck.exe" if os.name == "nt" else "camel-parsecheck"
    return str(home / "bin" / name)


def run(args):
    proc = subprocess.run([tool(), *args], cwd=ROOT, capture_output=True, text=True, encoding="utf-8")
    code, err = proc.returncode, proc.stderr
    if not 0 <= code <= 2:
        # Ended by a signal (negative on POSIX) or by an exception (an NTSTATUS such as
        # 0xC00000FD on Windows): neither is an exit code this script can pass on.
        err += f"\ncamel-parsecheck {args[0]} ended abnormally: exit code {code} ({code & 0xFFFFFFFF:#010x})\n"
        code = 3
    return code, proc.stdout, err


def repository_files():
    """Every .cml file of the repository, the handover directory aside: the input of the format
    and fuzz checks, which are not snapshots and gain from the breadth. The handover directory
    holds the drafts the language author works from, and a module joins this corpus where it is
    integrated into the standard library (corpus.HANDOVER)."""
    os.chdir(ROOT)
    return [str(p).replace("\\", "/") for p in corpus.repository_sources()]


def parse_cases():
    """The parser's own cases: the .cml files under test/cases/parse. They lead the snapshot."""
    os.chdir(ROOT)
    return [str(p).replace("\\", "/") for p in corpus.parse_cases()]


def snapshot_corpus():
    """What the snapshot records, in the order the record holds it: the parser's own cases first,
    then every other .cml file of the repository.

    The order is part of the record. The cases and the probes keep the lines they were recorded
    on, and a file new to the record is appended, so a diff of the golden shows what moved and
    what was added instead of a reordering of three thousand lines. The rest of the repository is
    corpus.repository_sources(), which leaves the handover directory out: those are the drafts a
    module is integrated from, and the module enters the corpus as stdlib/<module>.cml.
    """
    cases = parse_cases()
    seen = set(cases)
    return cases, [path for path in repository_files() if path not in seen]


def filter_files(files, patterns):
    if not patterns:
        return list(files)
    return [path for path in files if any(fnmatch.fnmatchcase(path, pattern) for pattern in patterns)]


def corpus_gate(mode, files, patterns, allow_empty):
    """Refuse a corpus smaller than the tree's own parse corpus.

    The floor is `len(corpus.parse_cases())`, counted from the tree on every run: adding or
    removing a case under test/cases/parse moves it, and no literal here can go stale. `--only`
    is a deliberate subset -- a single-file run for debugging -- so it is allowed, but its size is
    printed next to the floor. An empty selection still needs `--allow-empty REASON`, and the
    reason is printed, so nothing passes silently.

    What makes this red: cut repository_files() below the tree's parse corpus (or pass
    `--only` a pattern that matches nothing) and run `format`/`fuzz` without `--allow-empty`;
    the run exits 1 with the expected and the got counts instead of printing changed=0 /
    failures=0.
    """
    expected = len(parse_cases())
    got = len(files)
    if got == 0 and allow_empty:
        print(f"{mode}: 0 file(s) selected; the tree's parse corpus has {expected}; "
              f"--allow-empty: {allow_empty}")
        return 0
    if got == 0:
        print(f"{mode}: expected at least {expected} file(s) (the tree's parse corpus, "
              f"test/cases/parse/**), got 0; refusing to check nothing")
        return 1
    if patterns:
        print(f"{mode}: targeted run over {got} file(s) selected by --only; "
              f"the tree's parse corpus has {expected}")
        return 0
    if got < expected:
        if allow_empty:
            print(f"{mode}: expected at least {expected} file(s) (the tree's parse corpus, "
                  f"test/cases/parse/**), got {got}; --allow-empty: {allow_empty}")
            return 0
        print(f"{mode}: expected at least {expected} file(s) (the tree's parse corpus, "
              f"test/cases/parse/**), got {got}; refusing a corpus smaller than the parser's own")
        return 1
    return 0


def write_list(folder, files):
    path = Path(folder) / "list.txt"
    path.write_text("\n".join(files) + "\n", encoding="utf-8", newline="\n")
    return str(path)


def write_probes(folder):
    """Generate the probes and return (actual paths, the names they are recorded under)."""
    saved = sys.argv
    sys.argv = ["probes.py", folder]
    try:
        probes.main()
    finally:
        sys.argv = saved
    actual = sorted(str(p).replace("\\", "/") for p in Path(folder).glob("probe_*.cml"))
    return actual, [f"probes/{Path(path).name}" for path in actual]




def read_golden():
    """The recorded lines, keyed by the path after the hash and the verdict."""
    if not GOLDEN.exists():
        return {}
    was = {}
    for number, line in enumerate(GOLDEN.read_text(encoding="utf-8").splitlines(), 1):
        parts = line.split(" ", 2)
        if len(parts) != 3:
            raise SystemExit(f"{GOLDEN.relative_to(ROOT)}:{number}: not a snapshot line: {line!r}")
        was[parts[2]] = line
    return was


def run_hashes(folder, files):
    code, out, err = run(["hashes", write_list(folder, files)])
    if code != 0:
        print(err or out)
        return None, code
    prefix = str(Path(folder)).replace("\\", "/") + "/"
    now = {}
    for line in (line.replace(prefix, "probes/") for line in out.splitlines()):
        parts = line.split(" ", 2)
        if len(parts) != 3:
            print(f"hashes: unexpected output line: {line!r}")
            return None, 1
        now[parts[2]] = line
    return now, 0


def snapshot(update, patterns):
    was = read_golden()
    cases, others = snapshot_corpus()
    recorded = cases + others
    chosen = filter_files(recorded, patterns)
    if patterns and not chosen:
        print(f"snapshot: --only selected none of the tree's {len(recorded)} .cml file(s); "
              f"refusing to update/compare nothing")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        probe_files, probe_names = write_probes(tmp)
        if update:
            # A file the record does not hold yet is hashed even when --only does not select it:
            # the record must cover the tree, and a targeted update may not leave a gap.
            unrecorded = [path for path in recorded if path not in was and path not in chosen]
            to_hash = list(dict.fromkeys(chosen + unrecorded)) + probe_files
        else:
            to_hash = chosen + probe_files
        if not to_hash:
            print(f"snapshot: the selection is empty; the tree has {len(recorded)} .cml file(s) "
                  f"besides the probes; refusing to compare nothing")
            return 1
        now, code = run_hashes(tmp, to_hash)
        if now is None:
            return code

        canonical = cases + probe_names + others
        if update:
            merged = {}
            for path in cases + others:
                line = now.get(path, was.get(path))
                if line is not None:
                    merged[path] = line
            for name in probe_names:
                line = now.get(name, was.get(name))
                if line is not None:
                    merged[name] = line
            # A recorded line outside the corpus is dropped rather than kept: the corpus is the
            # repository, so a line it does not hold is either a probe probes.py no longer makes
            # or a file of the handover directory, and neither is a reading of the tree.
            missing = [path for path in canonical if path not in merged]
            if missing:
                print(f"snapshot: the tree records {len(canonical)} entries but the merged list "
                      f"would cover {len(merged)}; refusing a record that covers less: "
                      f"{', '.join(missing[:5])}{' ...' if len(missing) > 5 else ''}")
                return 1
            order = canonical
            text = "\n".join(merged[path] for path in order) + "\n"
            GOLDEN.write_text(text, encoding="utf-8", newline="\n")
            kept = len(merged) - len(now)
            print(f"wrote {GOLDEN.relative_to(ROOT)} ({len(merged)} files: {len(cases)} parse cases "
                  f"+ {len(probe_names)} probes + {len(others)} other .cml files of the "
                  f"repository; re-hashed {len(now)} line(s), kept {kept} recorded line(s) of the "
                  f"{len(was)})")
            return 0

        compared = 0
        bad = 0
        added = 0
        for path, line in now.items():
            if path not in was:
                added += 1  # a new test case: recorded at the next --update
            elif was[path] != line:
                print(f"read differently: {path}\n    was {was[path][:19]}\n    now {line[:19]}")
                bad += 1
            else:
                compared += 1
        # A recorded line whose file the tree no longer has is a record that claims more than the
        # tree holds; a recorded line the corpus does not hold is a reading nothing checks. Both
        # used to be read as a pass because only the current list was walked.
        recorded_probes = set(probe_names)
        corpus_set = set(canonical)
        for path in was:
            if path in now or path in corpus_set:
                continue
            if path.startswith("probes/"):
                if path not in recorded_probes:
                    print(f"no such probe in probes.py: {path}")
                    bad += 1
            elif not (ROOT / path).is_file():
                print(f"recorded file no longer exists: {path}")
                bad += 1
            else:
                print(f"recorded file outside the snapshot's corpus: {path}")
                bad += 1
        unrecorded = [path for path in canonical if path not in was and path not in now]
        if unrecorded:
            print(f"{len(unrecorded)} file(s) not in the snapshot yet: "
                  f"{', '.join(unrecorded[:5])}{' ...' if len(unrecorded) > 5 else ''}")
        if bad:
            print(f"{bad} difference(s). If the change is intended: "
                  f"python3 test/tools/parsecheck/check.py snapshot --update")
            return 1
        print(f"ast snapshot: {compared} files read as recorded, {added} not in the snapshot yet, "
              f"of {len(canonical)} in the corpus")
        if patterns:
            print(f"  (--only re-hashed {len(chosen)} of the {len(recorded)} .cml file(s); "
                  f"{len(was)} recorded line(s) left untouched)")
        return 0


def format_check(patterns, allow_empty):
    files = filter_files(repository_files(), patterns)
    gate = corpus_gate("format", files, patterns, allow_empty)
    if gate != 0:
        return gate
    if not files:
        return 0  # --allow-empty printed its reason; there is nothing to run the tool on
    with tempfile.TemporaryDirectory() as tmp:
        code, out, err = run(["fmtcheck", write_list(tmp, files)])
    text = (out + err).strip()
    print(text)
    summary = re.search(r"FMTCHECK files=(\d+)", text)
    if summary is None:
        print("format: the tool printed no FMTCHECK summary; refusing to read a missing verdict")
        return 1
    examined = int(summary.group(1))
    if examined != len(files):
        print(f"format: the list names {len(files)} file(s) but the tool examined {examined}; "
              f"refusing a run that checked less than the corpus")
        return 1
    # The comment verdict is a property of the formatter, and a run that read no comment cannot
    # state it: the same rule as the corpus floor above, for the other thing this check counts.
    # `dropped=0` is asserted by the plan (test/plans/feat/parse/frontend.plan.toml); this refuses
    # the run in which the count would be a statement about nothing.
    found = re.search(r"comments=(\d+)", text)
    if found is None or int(found.group(1)) == 0:
        print("format: the tool reported no comment examined; a check on zero comments is not a "
              "pass (comments=0 or no comments= in the FMTCHECK summary)")
        return 1
    # `dropped=0`, `duplicated=0` and `unplaced=0` are asserted by the plan; this refuses a summary
    # that carries no verdict on them, so a tool too old to report one cannot pass by saying nothing.
    if (re.search(r"dropped=(\d+)", text) is None or re.search(r"duplicated=(\d+)", text) is None
            or re.search(r"unplaced=(\d+)", text) is None):
        print("format: the FMTCHECK summary reports no dropped=, no duplicated= or no unplaced= "
              "count; refusing to read a verdict the tool did not state")
        return 1
    return code


def fuzz(iterations, seed, patterns, allow_empty):
    files = filter_files(repository_files(), patterns)
    gate = corpus_gate("fuzz", files, patterns, allow_empty)
    if gate != 0:
        return gate
    if not files:
        return 0  # --allow-empty printed its reason; there is nothing to run the tool on
    with tempfile.TemporaryDirectory() as tmp:
        code, out, err = run(["fuzz", write_list(tmp, files), str(iterations), str(seed), tmp])
        print((out + err).strip())
        if code != 0:
            for path in sorted(Path(tmp).glob("fail_*.cml"))[:3]:
                print(f"---- {path.name}\n{path.read_text(encoding='utf-8', errors='replace')[:1500]}")
    return code


def main(argv):
    ap = argparse.ArgumentParser(prog="check.py", description="the front end's corpus checks")
    sub = ap.add_subparsers(dest="mode", required=True)

    snap = sub.add_parser("snapshot", help="compare (or record) the ASTs of the parse corpus")
    snap.add_argument("--update", action="store_true", help="merge the selected readings into the golden")
    snap.add_argument("--only", action="append", default=[], metavar="GLOB",
                      help="re-hash only the parse cases matching GLOB (repeatable)")

    fmt = sub.add_parser("format", help="formatting every accepted file keeps its AST")
    fmt.add_argument("--only", action="append", default=[], metavar="GLOB",
                     help="check only the files matching GLOB (repeatable)")
    fmt.add_argument("--allow-empty", metavar="REASON", default=None,
                     help="allow an empty selection, and print REASON")

    fz = sub.add_parser("fuzz", help="mutated programs give a lossless tree")
    fz.add_argument("iterations", nargs="?", type=int, default=3000)
    fz.add_argument("seed", nargs="?", type=int, default=1)
    fz.add_argument("--only", action="append", default=[], metavar="GLOB",
                    help="mutate only the files matching GLOB (repeatable)")
    fz.add_argument("--allow-empty", metavar="REASON", default=None,
                    help="allow an empty selection, and print REASON")

    args = ap.parse_args(argv[1:])
    if args.mode == "snapshot":
        return snapshot(args.update, args.only)
    if args.mode == "format":
        return format_check(args.only, args.allow_empty)
    return fuzz(args.iterations, args.seed, args.only, args.allow_empty)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
