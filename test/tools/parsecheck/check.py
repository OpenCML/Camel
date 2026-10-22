#!/usr/bin/env python3
"""The corpus checks of the front end, as the test plan runs them.

    python3 test/tools/parsecheck/check.py snapshot [--update] [--only GLOB ...]
    python3 test/tools/parsecheck/check.py format   [--only GLOB ...] [--allow-empty REASON]
    python3 test/tools/parsecheck/check.py fuzz     [ITERATIONS [SEED]] [--only GLOB ...]
                                                    [--allow-empty REASON]

snapshot  Every .cml case of the parse corpus (test/cases/parse) and every probe (probes.py) is
          parsed; a hash of the normalized AST of each (or of its diagnostics, when it is
          rejected) is compared with test/plans/feat/parse/golden/ast_hashes.txt. The golden list
          was recorded when the hand-written parser was last compared, tree for tree, with the
          ANTLR parser it replaced; a line that changes means the parser reads that file
          differently. The snapshot is a snapshot of the parser's own tests, not of the
          repository (H188): a .cml file outside test/cases/parse is checked by the plan that
          owns it, and editing one does not move this file.
          --update merges into the recorded list: the selected lines are re-hashed and replaced,
          a file the list does not hold yet is hashed and added, every other line whose file
          still exists is kept, and a line whose file is gone is dropped. The merged list must
          cover every case corpus.parse_cases() selects and every probe, or the update refuses
          and names the gaps. It merges rather than refusing a partial update because recording
          one changed case should not depend on re-reading the other 273; the filter can then
          never shrink the record (H199).
          --only GLOB restricts what is re-hashed, not what the record must cover; without it
          the whole corpus is refreshed.
format    Formatting every accepted file of the repository gives a text with the same AST.
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
    """Every .cml file of the repository: the input of the format and fuzz checks, which are not
    snapshots and gain from the breadth (H188 kept them as they were)."""
    os.chdir(ROOT)
    return [str(p).replace("\\", "/") for p in corpus.walk(Path("."), ".cml")]


def parse_cases():
    """The parse corpus: the cases under test/cases/parse. This is what the snapshot hashes."""
    os.chdir(ROOT)
    return [str(p).replace("\\", "/") for p in corpus.parse_cases()]


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
    cases = parse_cases()
    chosen = filter_files(cases, patterns)
    if patterns and not chosen:
        print(f"snapshot: --only selected none of the tree's {len(cases)} parse case(s); "
              f"refusing to update/compare nothing")
        return 1
    with tempfile.TemporaryDirectory() as tmp:
        probe_files, probe_names = write_probes(tmp)
        if update:
            # A file the record does not hold yet is hashed even when --only does not select it:
            # the record must cover the tree, and a targeted update may not leave a gap.
            unrecorded = [path for path in cases if path not in was and path not in chosen]
            to_hash = list(dict.fromkeys(chosen + unrecorded)) + probe_files
        else:
            to_hash = chosen + probe_files
        if not to_hash:
            print(f"snapshot: the selection is empty; the tree's parse corpus has {len(cases)} "
                  f"case(s); refusing to compare nothing")
            return 1
        now, code = run_hashes(tmp, to_hash)
        if now is None:
            return code

        canonical = cases + probe_names
        if update:
            merged = {}
            for path in cases:
                line = now.get(path, was.get(path))
                if line is not None and (path in now or (ROOT / path).is_file()):
                    merged[path] = line
            for name in probe_names:
                line = now.get(name, was.get(name))
                if line is not None:
                    merged[name] = line
            # A recorded line outside the corpus is kept while its file still exists; H188
            # removed the old repository-wide lines, and a stale one is not kept.
            for path, line in was.items():
                if path in merged or path in canonical:
                    continue
                if not path.startswith("probes/") and (ROOT / path).is_file():
                    merged[path] = line
            missing = [path for path in canonical if path not in merged]
            if missing:
                print(f"snapshot: the tree records {len(canonical)} entries but the merged list "
                      f"would cover {len(merged)}; refusing a record that covers less: "
                      f"{', '.join(missing[:5])}{' ...' if len(missing) > 5 else ''}")
                return 1
            order = cases + probe_names + [path for path in merged if path not in canonical]
            text = "\n".join(merged[path] for path in order) + "\n"
            GOLDEN.write_text(text, encoding="utf-8", newline="\n")
            kept = len(merged) - len(now)
            print(f"wrote {GOLDEN.relative_to(ROOT)} ({len(merged)} files: {len(cases)} cases + "
                  f"{len(probe_names)} probes; re-hashed {len(now)} line(s), kept {kept} recorded "
                  f"line(s) of the {len(was)})")
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
        # tree holds; it used to be ignored because only the current list was walked.
        recorded_probes = set(probe_names)
        for path in was:
            if path in now:
                continue
            if path.startswith("probes/"):
                if path not in recorded_probes:
                    print(f"no such probe in probes.py: {path}")
                    bad += 1
            elif not (ROOT / path).is_file():
                print(f"recorded file no longer exists: {path}")
                bad += 1
        unrecorded = [path for path in canonical if path not in was and path not in now]
        if unrecorded:
            print(f"{len(unrecorded)} file(s) not in the snapshot yet: "
                  f"{', '.join(unrecorded[:5])}{' ...' if len(unrecorded) > 5 else ''}")
        if bad:
            print(f"{bad} difference(s). If the change is intended: "
                  f"python3 test/tools/parsecheck/check.py snapshot --update")
            return 1
        print(f"ast snapshot: {compared} files read as recorded, {added} not in the snapshot yet")
        if patterns:
            print(f"  (--only re-hashed {len(chosen)} of the {len(cases)} case(s); "
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
