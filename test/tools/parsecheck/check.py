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
          A re-record is a whole-table rewrite of the lines it covers, so a line it does not
          produce leaves the record with the row. What it may take out is decided before anything
          is written (`drop_guard`): a dropped line whose .cml file this tree still has, or that
          the base branch still has, is refused, and so is every row a *bulk* re-record (one
          without `--only`) would drop -- because a bulk re-record taken on a tree that is missing
          some files deletes their rows and the loss lands on a tree nobody touched. That is what
          happened on 2026-11-17: `eaa3ecb8c` re-recorded with `-UpdateAstHashes -AstOnly` on a tree
          that did not hold the seven `test/cases/compile/var/loop_*.cml` a parallel lane had just
          added, the record went from 4400 lines to 4393, and `frontend.ast_snapshot` was the thing
          that said so, a layer later (`agents/test.md`). `--drop PATH` names a line that really
          should go (a deleted case), and is the only way a bulk re-record loses a row:

              --drop PATH   drop this recorded line on purpose (repeatable; with --update only)
              --base REF    where a dropped line's file is looked for when the disk lacks it
                            (default origin/develop; `--base ''` reads the disk alone)
format    Formatting every accepted file of the repository gives a text with the same AST, keeps
          every comment of the input, and is a fixed point (the text formats to itself). A file
          that loses a comment, or whose second pass differs, is named and fails the check.
fuzz      Mutated programs of the repository: the parser returns a lossless tree for each
          (camel-parsecheck fuzz).

--self-test runs no parser: it is the record guard above on a miniature tree -- a record of three
          lines with two `.cml` files on disk -- and it checks that the guard refuses to write a
          record that loses a line whose file is still there. It needs no build, so it is the one
          reading of this tool a lane can take in its own worktree.

A check that examines nothing is not a pass (agents/test.md). `format` and `fuzz` count the files
corpus.parse_cases() selects -- the parser's own corpus, counted from the tree on every run, never
written down -- and refuse a corpus smaller than that count. `--only` marks a deliberate subset
(down to one file, for a targeted run); an empty subset still needs `--allow-empty REASON`, which
is printed, so no run passes without saying what it examined. The same rule sits in the C++ tool:
hashes, fmtcheck and fuzz refuse an empty list (and fuzz refuses a list with no seed files).

Run from the repository root; the tools are those of out/latest unless CAMEL_HOME is set.
"""
import argparse
import contextlib
import fnmatch
import io
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




def parse_snapshot(text, where):
    """The recorded lines of a snapshot text, keyed by the path after the hash and the verdict."""
    was = {}
    for number, line in enumerate(text.splitlines(), 1):
        parts = line.split(" ", 2)
        if len(parts) != 3:
            raise SystemExit(f"{where}:{number}: not a snapshot line: {line!r}")
        was[parts[2]] = line
    return was


def read_golden():
    """The recorded lines, keyed by the path after the hash and the verdict."""
    if not GOLDEN.exists():
        return {}
    return parse_snapshot(GOLDEN.read_text(encoding="utf-8"), GOLDEN.relative_to(ROOT))


# The prefix a probe is recorded under (`run_hashes`). probes.py writes the probes into a temporary
# directory, so no file of the tree answers to that name: a probe line that goes is a probe
# probes.py no longer makes, and it says nothing about a file the repository lost.
PROBE_PREFIX = "probes/"


def git_ok(args):
    """Did this git command succeed? A missing `git`, a missing revision and a missing path are all
    the same answer here, and none of them is fatal: the guard is a refusal on top of a record, and
    a question it cannot ask is answered by the other question it does ask."""
    try:
        proc = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    except OSError:
        return False
    return proc.returncode == 0


def drop_guard(was, merged, drops, root, base, bulk, where):
    """Refuse a record that loses a row while the file it records is still part of the repository.

    The record is a snapshot of the repository, not of the tree the recording ran in, so a line
    leaving it says the repository lost that file. Three things together are the failure -- the
    merged list is shorter than the file on disk, the lines it drops still name a `.cml` the
    repository has, and none of them was named by `--drop` -- and then nothing is written: every
    lost row is printed with the hash it recorded, where its file still is, and the two ways out.
    A row whose file is in neither place is a case that was deleted, and it leaves the record with a
    line that counts it.

    Why the second and third kind are not enough on their own (`agents/test.md`): on 2026-11-17
    `eaa3ecb8c` re-recorded with `-UpdateAstHashes -AstOnly` on a tree that did not hold the seven
    `test/cases/compile/var/loop_*.cml` a parallel lane had just added. The re-record rewrote the
    whole table, the seven rows went with the files the tree did not have, the record fell from 4400
    lines to 4393, and `frontend.ast_snapshot` went red on a tree nobody had touched; `27de8a3df`
    put the rows back by hand. Those files were in neither that tree nor its base branch at that
    moment, so no check of where a file is could have seen the loss -- but a bulk re-record knew it
    was rewriting the whole table. So the widest form (no `--only`) may not lose a row at all unless
    `--drop` names it. The narrow form re-hashes the cases it names and rewrites only their lines, so
    the only rows it can lose are those of files that are gone -- which is the deletion it is allowed
    to carry out, counted and printed.

    Returns 0 when the merged list may be written, 1 when it may not.
    """
    named = list(drops)
    kept = set(merged)
    rows = [path for path in was if path not in kept]
    probes = [path for path in rows if path.startswith(PROBE_PREFIX)]
    rows = [path for path in rows if not path.startswith(PROBE_PREFIX)]
    unused = [path for path in named if path not in rows]
    rows = [path for path in rows if path not in named]

    here, behind, gone = [], [], []
    if rows:
        on_disk = {path: (root / path).is_file() for path in rows}
        here = [path for path in rows if on_disk[path]]
        rest = [path for path in rows if not on_disk[path]]
        lookup = bool(rest and base)
        if lookup and not git_ok(["rev-parse", "--verify", "--quiet", f"{base}^{{commit}}"]):
            # The host that takes the record holds the lane's commits and not the base branch
            # (`camel-remote.ps1`), so this question has no answer there. It is said out loud
            # rather than answered with "no": a check that could not run and one that passed are
            # the two readings this tool must never print the same way.
            print(f"snapshot: {base} is not readable in this tree; a dropped line's file is looked "
                  f"for on the disk only")
            lookup = False
        behind = [path for path in rest if lookup and git_ok(["cat-file", "-e", f"{base}:{path}"])]
        gone = [path for path in rest if path not in behind]

    def show(paths):
        for path in paths[:20]:
            called = ("on disk" if path in here else f"in {base}" if path in behind
                      else "not on disk, not in the base branch")
            print(f"snapshot:   {was[path]}   ({called})")
        if len(paths) > 20:
            print(f"snapshot:   ... and {len(paths) - 20} more")

    if here or behind:
        went = (f"from {len(was)} line(s) to {len(merged)}" if len(merged) < len(was)
                else f"keeping {len(merged)} line(s) of the {len(was)}")
        print(f"snapshot: refusing to write: {where} would go {went}, and "
              f"{len(here) + len(behind)} dropped line(s) still name a .cml file the repository has:")
        show(here + behind)
        print("snapshot: the file of each of those lines is here, so this is not a case that was "
              "deleted -- the tree the record was taken in does not hold it. Either re-record on a "
              "tree that has it (`git merge origin/develop` first), or say the lines should go:")
        print("snapshot:   python3 test/tools/parsecheck/check.py snapshot --update --drop <path> "
              "[--drop <path> ...]")
        print(f"snapshot: nothing was written; {where} still holds {len(was)} line(s)")
        return 1
    if bulk and gone:
        print(f"snapshot: refusing to write: this is a bulk re-record (no --only) and it would go "
              f"from {len(was)} line(s) to {len(merged)}, dropping {len(gone)} recorded row(s):")
        show(gone)
        print("snapshot: a bulk re-record rewrites the whole table, so the rows of files this tree "
              "does not hold leave with it -- a parallel lane's cases disappear and "
              "frontend.ast_snapshot goes red on a tree nobody touched (2026-11-17, 4400 lines to "
              "4393). Merge origin/develop first and re-record there, or name the rows that really "
              "should go:")
        print("snapshot:   python3 test/tools/parsecheck/check.py snapshot --update --drop <path> "
              "[--drop <path> ...]")
        print("snapshot: one case at a time needs no list: --only '<pattern>' re-hashes the cases it "
              "names and rewrites only their lines")
        print(f"snapshot: nothing was written; {where} still holds {len(was)} line(s)")
        return 1
    if gone:
        # The rows may go, and the run says how many and which: "the record got shorter" is the
        # reading that has to be in the output, not something a reader works out of a line count.
        print(f"snapshot: dropped {len(gone)} line(s) whose .cml file no longer exists "
              f"(\u5220\u6389 {len(gone)} \u6761\uff0c\u5bf9\u5e94\u6587\u4ef6\u5df2\u4e0d\u5b58\u5728): "
              + ", ".join(gone[:10]) + (f" ... and {len(gone) - 10} more" if len(gone) > 10 else ""))
    if probes:
        print(f"snapshot: dropped {len(probes)} probe line(s) probes.py no longer makes: "
              + ", ".join(probes[:10]) + (f" ... and {len(probes) - 10} more" if len(probes) > 10 else ""))
    if unused:
        print(f"snapshot: --drop named {len(unused)} path(s) that no line was dropped for: "
              + ", ".join(unused))
    return 0


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


def snapshot(update, patterns, drops=(), base="origin/develop"):
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
            # Nothing is written until the record is allowed to lose what it would lose: the guard
            # reads the file on disk, the base branch and whether this run is the bulk form, and
            # the order matters -- a record that is written and then found wrong has already cost
            # the next reader a night of red on a tree nobody touched.
            gate = drop_guard(was, merged, drops, ROOT, base, not patterns,
                              str(GOLDEN.relative_to(ROOT)))
            if gate != 0:
                return gate
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


def _self_test():
    """The record guard on a miniature tree: three recorded lines, two `.cml` files on disk.

    The shape is the one the rule is for, and it is the loss of 2026-11-17 in miniature: the tree
    holds two of the record's three `.cml` files, and the merged list drops the line of the second
    one -- the probe line stays, because a probe is recorded under a name no file answers to. The
    guard must refuse, print that one path with the hash it recorded, and write nothing.

    What makes it red: a `drop_guard` that returns 0 for a merged list losing a line whose file is on
    disk, or that prints no path -- the record then loses the row quietly, which is the loss
    `agents/test.md` describes (4400 lines to 4393, found a layer later by `frontend.ast_snapshot`).
    The reader runs over the other three answers as well, so a guard that refuses everything fails
    here too: a deleted case (its file gone, narrow form) may leave the record and is counted; a bulk
    re-record may not drop that same row; and `--drop` naming it is the way through.

    No parser and no build: this is the one reading of this tool a lane can take on its own tree.
    """
    failures = []

    def want(condition, complaint):
        if not condition:
            failures.append(complaint)

    with tempfile.TemporaryDirectory() as tmp:
        root = Path(tmp)
        (root / "test" / "cases" / "parse").mkdir(parents=True)
        for name in ("a", "b"):
            (root / "test" / "cases" / "parse" / f"{name}.cml").write_text(
                "func main() { return }\n", encoding="utf-8")

        line_a = "1111111111111111 ok test/cases/parse/a.cml"
        line_b = "3333333333333333 ok test/cases/parse/b.cml"
        line_c = "4444444444444444 ok test/cases/parse/c.cml"
        line_probe = "2222222222222222 ok probes/probe_d001.cml"

        def run(drops, bulk, was, merged):
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                verdict = drop_guard(was, merged, drops, root, "", bulk, "the record")
            return verdict, out.getvalue()

        # 1. Three recorded lines, two `.cml` files on disk, and the line whose file is on disk is
        #    the one the merge drops -- the incident's shape on the tree side of it.
        was = {"test/cases/parse/a.cml": line_a, "test/cases/parse/b.cml": line_b,
               "probes/probe_d001.cml": line_probe}
        merged = {"test/cases/parse/a.cml": line_a, "probes/probe_d001.cml": line_probe}
        verdict, text = run((), False, was, merged)
        print("--- a record of 3 lines and 2 .cml files on disk; the line for b.cml is dropped")
        print(text.rstrip("\n"))
        want(verdict == 1, f"the guard wrote the record anyway (verdict {verdict})")
        want("refusing to write" in text, "the run did not say it refused")
        want("test/cases/parse/b.cml" in text, "the refusal did not name test/cases/parse/b.cml")
        want(line_b in text, "the refusal did not print the hash recorded for b.cml")
        want("git merge origin/develop" in text, "the refusal did not name the way out")

        # 2. The file is gone and the run is the narrow form: the row leaves, and the run counts it.
        was_gone = {"test/cases/parse/a.cml": line_a, "test/cases/parse/c.cml": line_c}
        merged_gone = {"test/cases/parse/a.cml": line_a}
        verdict, text = run((), False, was_gone, merged_gone)
        print("--- the dropped line's .cml file is gone, narrow form: the row may go, counted")
        print(text.rstrip("\n"))
        want(verdict == 0, f"a deleted case could not leave the record (verdict {verdict})")
        want("删掉 1 条，对应文件已不存在" in text,
             "the run did not print how many rows went and that their file is gone")

        # 3. The same row, dropped by a bulk re-record: refused, because a bulk re-record cannot
        #    tell a deleted case from a tree that is missing somebody else's files.
        verdict, text = run((), True, was_gone, merged_gone)
        print("--- the same row, dropped by a bulk re-record: refused")
        print(text.rstrip("\n"))
        want(verdict == 1, f"a bulk re-record dropped a row unasked (verdict {verdict})")
        want("test/cases/parse/c.cml" in text, "the bulk refusal did not name the row it would drop")

        # 4. Named, the same row goes in either form.
        verdict, text = run(("test/cases/parse/b.cml",), True, was, merged)
        print("--- the same row with --drop naming it: written")
        print(text.rstrip("\n") or "snapshot: (no refusal)")
        want(verdict == 0, f"--drop naming the row did not let it go (verdict {verdict})")
        want("refusing" not in text, "--drop named the row and the guard still refused")

    for failure in failures:
        print(f"ast snapshot guard self-test: FAILED: {failure}")
    if failures:
        return 1
    print("ast snapshot guard self-test: OK")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(prog="check.py", description="the front end's corpus checks")
    ap.add_argument("--self-test", action="store_true",
                    help="check the record guard on a miniature tree and exit (needs no build)")
    # Not `required=True`: `--self-test` is a mode of its own and runs before any subcommand, and a
    # missing mode is refused below with the same exit code argparse would have used.
    sub = ap.add_subparsers(dest="mode")

    snap = sub.add_parser("snapshot", help="compare (or record) the ASTs of the parse corpus")
    snap.add_argument("--update", action="store_true", help="merge the selected readings into the golden")
    snap.add_argument("--only", action="append", default=[], metavar="GLOB",
                      help="re-hash only the parse cases matching GLOB (repeatable)")
    snap.add_argument("--drop", action="append", default=[], metavar="PATH",
                      help="with --update: this recorded line really should go, so the guard lets it "
                           "(repeatable; a bulk re-record takes a row out only this way)")
    snap.add_argument("--base", default="origin/develop", metavar="REF",
                      help="with --update: where a dropped line's file is looked for when the disk "
                           "lacks it (default origin/develop; an empty REF reads the disk alone)")

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
    if args.self_test:
        return _self_test()
    if args.mode is None:
        ap.print_usage(sys.stderr)
        print("check.py: a mode is required (snapshot | format | fuzz), or --self-test",
              file=sys.stderr)
        return 2
    if args.mode == "snapshot":
        if args.drop and not args.update:
            print("check.py: --drop names a line a re-record may lose; give --update, or drop --drop",
                  file=sys.stderr)
            return 2
        return snapshot(args.update, args.only, args.drop, args.base)
    if args.mode == "format":
        return format_check(args.only, args.allow_empty)
    return fuzz(args.iterations, args.seed, args.only, args.allow_empty)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
